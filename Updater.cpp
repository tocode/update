#include "Updater.h"
#include <utility>
#include "cocos2d.h"
#include <curl/curl.h>
#include <curl/easy.h>
#include <unzip/unzip.h>

USING_NS_CC;

#define LOW_SPEED_LIMIT 1L
#define LOW_SPEED_TIME 5L
#define BUFFER_SIZE    8192
#define MAX_FILENAME   512
#define UPLOG(format,...) CCLOG("[updater]" format,##__VA_ARGS__)

size_t write_function(void *data, size_t size, size_t nmemb, void *bind_data)
{
	UPLOG("size=%d,nmemb=%d", size, nmemb);
	FILE *fp = (FILE*)bind_data;
	size_t written = fwrite(data, size, nmemb, fp);
	return written;
}
int progress_function(void *bind_data, double total_down, double now_down, double total_up, double now_up)
{
	if (total_down > 0)
	{
		float percent = now_down / total_down;
		UPLOG("downloading... %.2f", percent);
	}
	return 0;
}
std::string split_filename(const std::string& url)
{
	size_t length = url.length();
	size_t index = url.find_last_of('/');
	bool finded = index != std::string::npos;
	return finded ? url.substr(index + 1, length - (index + 1)) : url;
}
std::string split_path(const std::string& url)
{
	size_t length = url.length();
	size_t index = url.find_last_of('/');
	bool finded = index != std::string::npos;
	return finded ? url.substr(0, index + 1) : "";
}
bool download(const std::string& url, const std::string& local_path, const std::string& local_name)
{
	// Create a file to save package.
	const std::string tempfilename = local_path + local_name;
	FILE *fp = fopen(tempfilename.c_str(), "wb");
	if (!fp)
	{
		UPLOG("can not create file %s", tempfilename.c_str());
		return false;
	}

	// Download pacakge
	CURL* curl = curl_easy_init();
	if (!curl)
	{
		UPLOG("can not init curl");
		return false;
	}
	CURLcode res;
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_function);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, false);
	curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_function);
	//curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, this);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, LOW_SPEED_LIMIT);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, LOW_SPEED_TIME);

	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (res != 0)
	{
		UPLOG("error when download package");
		fclose(fp);
		return false;
	}

	UPLOG("succeed downloading package %s", url.c_str());

	fclose(fp);
	return true;
}
/*
bool go_download(std::string url)
{
	auto t = std::thread(downLoad,url);
	t.detach();
	return true;
}*/
bool createDirectory(const char *path)
{
#if (CC_TARGET_PLATFORM != CC_PLATFORM_WIN32)
	mode_t processMask = umask(0);
	int ret = mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO);
	umask(processMask);
	if (ret != 0 && (errno != EEXIST))
	{
		return false;
	}

	return true;
#else
	BOOL ret = CreateDirectoryA(path, nullptr);
	if (!ret && ERROR_ALREADY_EXISTS != GetLastError())
	{
		return false;
	}
	return true;
#endif
}

bool extract_file(unzFile zip_file, const std::string& entry_fullname, const std::string& unzip_fullname)
{
	// Open current file.
	if (unzOpenCurrentFile(zip_file) != UNZ_OK)
	{
		UPLOG("can not open file %s", entry_fullname);
		return false;
	}

	// Create a file to store current file.
	FILE *unzip_file = fopen(unzip_fullname.c_str(), "wb");
	if (!unzip_file)
	{
		UPLOG("can not open destination file %s", unzip_fullname.c_str());
		unzCloseCurrentFile(zip_file);
		return false;
	}

	// Write current file content to destinate file.
	char buffer[BUFFER_SIZE];
	int bytes;
	do
	{
		bytes = unzReadCurrentFile(zip_file, buffer, BUFFER_SIZE);
		if (bytes < 0)
		{
			UPLOG("can not read zip file %s, error code is %d", entry_fullname, bytes);
			unzCloseCurrentFile(zip_file);
			return false;
		}

		if (bytes > 0)
		{
			fwrite(buffer, bytes, 1, unzip_file);
		}
	} while (bytes > 0);

	fclose(unzip_file);

	unzCloseCurrentFile(zip_file);

	return true;
}
bool uncompress(const std::string& zip_path, const std::string& zip_name)
{
	// Open the zip file
	const std::string zip_fullname = zip_path + zip_name;
	unzFile zip_file = unzOpen(zip_fullname.c_str());
	if (!zip_file)
	{
		UPLOG("can not open downloaded zip file %s", zip_fullname.c_str());
		return false;
	}

	// Get info about the zip file
	unz_global_info zip_info;
	if (unzGetGlobalInfo(zip_file, &zip_info) != UNZ_OK)
	{
		UPLOG("can not read file global info of %s", zip_fullname.c_str());
		unzClose(zip_file);
		return false;
	}

	UPLOG("start uncompressing");

	// Loop to extract all files.
	for (uLong i = 0; i < zip_info.number_entry; ++i)
	{
		// Get info about current file.
		unz_file_info entry_info;
		char entry_fullname[MAX_FILENAME];
		if (unzGetCurrentFileInfo(zip_file, &entry_info, entry_fullname, MAX_FILENAME,	nullptr, 0,	nullptr, 0) != UNZ_OK)
		{
			UPLOG("can not read file info");
			unzClose(zip_file);
			return false;
		}
		std::string entry_path = split_path(entry_fullname);
		std::string entry_name = split_filename(entry_fullname);

		if (entry_path != "")
		{
			const std::string unzip_folder = zip_path + entry_path;
			if (!createDirectory(unzip_folder.c_str()))
			{
				UPLOG("can not create directory %s", unzip_folder.c_str());
				unzClose(zip_file);
				return false;
			}
		}

		if (entry_name != "")
		{
			if (extract_file(zip_file, entry_fullname, zip_path + entry_fullname) == false)
			{
				UPLOG("extract %s fail", entry_fullname);
				unzClose(zip_file);
			}
		}
		// Goto next entry listed in the zip file.
		if ((i + 1) < zip_info.number_entry)
		{
			if (unzGoToNextFile(zip_file) != UNZ_OK)
			{
				UPLOG("can not read next file");
				unzClose(zip_file);
				return false;
			}
		}
	}

	UPLOG("end uncompressing");
	unzClose(zip_file);

	return true;
}