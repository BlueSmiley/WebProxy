// UrlBlocker.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <vector>
#include <string>
#include <unordered_set>
#include <condition_variable>
#include <mutex>
#include "UrlBlocker.h"

using namespace std;


vector<string> split(const string& s, const string& delimiter) {
	size_t last = 0; 
	size_t next = 0; 
	vector<string> splitList;
	while (s.find(delimiter, last) != string::npos) 
	{ 
		next = s.find(delimiter, last);
		splitList.push_back(s.substr(last, next - last)); 
		last = next + delimiter.length(); 
	} 
	splitList.push_back(s.substr(last));
	return splitList;
}

/* 
User interface that handles standard input and output to requests of user
Lock comms before accesing queue 
*/
int user_interface(comms* m) {

	string arg;
	while (true) {
		
		getline(cin, arg);
		vector<string> argList = split(arg, " ");
		std::lock_guard<std::mutex> g(m->mutex);
		if (argList[0] == "list") {
			for (int i = 1; i < argList.size(); i++) {
				string url = argList[i];
				//Just to be safe....assuming there are no 0 length urls
				if (url.length() > 1 && m->banned_urls.find(url) != m->banned_urls.end()) {
					cout << url << endl;
				}
			}
			if (argList.size() == 1) {
				cout << "\nCurrently Blocking:\n";
				for (const auto& url : m->banned_urls) {
					cout << url << "\n";
				}
			}
		}
		else if (argList[0] == "ban") {
			for (int i = 1; i < argList.size(); i++) {
				string url = argList[i];
				//Just to be safe....assuming there are no 0 length urls
				if (url.length() > 1) {
					m->banned_urls.insert(url);
				}
			}
		}
		else if (argList[0] == "unban") {
			for (int i = 1; i < argList.size(); i++) {
				string url = argList[i];
				//Just to be safe....assuming there are no 0 length urls
				if (url.length() > 1) {
					m->banned_urls.erase(url);
				}
			}
		}
		else {
			cout << "\nUnrecognized command use one of: \n" <<
				"list <list of urls> - \n	If no urls given shows all currently banned urls.\n" <<
				"	If urls given then it shows all given urls which are in ban list \n" <<
				"ban <list of urls> - \n	adds all listed urls to ban list \n" <<
				"unban <list of urls> - \n	unbans listed urls \n" <<
				"<note:This syntax implies whitespace delimited options> \n";
		}
	}
}

int console(comms* messages)
{
	std::thread t1(user_interface, messages);
	while (true) {
		std::unique_lock<std::mutex> lock(messages->mutex);
		while (!messages->status)
			messages->condVar.wait(lock);
		std::queue<std::string> queue = messages->queue;
		//print out all accumalated messages
		while (!queue.empty()) {
			std::cout << queue.front() << "\n";
			queue.pop();
		}
		messages->status = false;
		messages->queue = queue;
	}

}


