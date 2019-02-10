// UrlBlocker.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <vector>
#include <string>
#include <unordered_set>
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

int process()
{
	//use hashmap instead of rolling own datastructures
	unordered_set<string> urlset;
	string arg;
	while (true) {
		cout << "\nCurrently Blocking:\n";
		for (const auto& url : urlset) {
			cout << url << "\n";
		}
		cout << endl;
		getline(cin, arg);
		vector<string> argList = split(arg, " ");
		if (argList[0] == "list") {
			for (int i = 1; i < argList.size(); i++) {
				string url = argList[i];
				//Just to be safe....assuming there are no 0 length urls
				if (url.length() > 1 && urlset.find(url) != urlset.end()) {
					cout << url << endl;
				}
			}
		}
		else if (argList[0] == "ban") {
			for (int i = 1; i < argList.size();i++) {
				string url = argList[i];
				//Just to be safe....assuming there are no 0 length urls
				if (url.length() > 1) {
					urlset.insert(url);
				}
			}
		}
		else if (argList[0] == "unban") {
			for (int i = 1; i < argList.size(); i++) {
				string url = argList[i];
				//Just to be safe....assuming there are no 0 length urls
				if (url.length() > 1) {
					urlset.erase(url);
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
		//std::cout << "Hello World!\n";
	}
}
