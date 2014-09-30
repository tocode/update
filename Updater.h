#ifndef UPDATER_H
#define UPDATER_H

#include <string>


bool download(const std::string& url, const std::string& local_path, const std::string& local_name);
//bool go_download(std::string url);
bool uncompress(const std::string& zip_path, const std::string& zip_name);
std::string split_filename(const std::string& url);
std::string split_path(const std::string& url);
#endif