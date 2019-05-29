#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <queue>  
#include <condition_variable>
#include <unordered_map>

struct comms {
	std::mutex mutex;
	std::queue<std::string> queue;
	std::condition_variable condVar;
	bool status;
	std::unordered_set<std::string> banned_urls;
};

struct cache_opts {
	bool is_public = false;
	bool wont_cache = false;
	bool ts_given = false;
	bool revalidate = false;
	bool etag_exists = false;
	std::string etag;
	std::chrono::system_clock::time_point validTime;
	std::string cacheData;
};

struct cache {
	std::mutex mutex;
	std::unordered_map<std::string, cache_opts> cache;
};

std::vector<std::string> split(const std::string& s, const std::string& delimiter);
int console(comms* messages);
