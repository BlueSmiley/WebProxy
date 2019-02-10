#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <queue>  

struct comms {
	std::mutex m;
	std::queue<std::string> queue;
};

std::vector<std::string> split(const std::string& s, const std::string& delimiter);
int process();
