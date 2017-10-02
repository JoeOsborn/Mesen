#include "stdafx.h"

//TODO: Use non-experimental namespace (once it is officially supported by VC & GCC)
#include "boost/filesystem.hpp"
namespace fs = boost::filesystem;

#include <unordered_set>
#include <algorithm>
#include "FolderUtilities.h"
#include "UTF8Util.h"

string FolderUtilities::_homeFolder = "";
string FolderUtilities::_saveFolderOverride = "";
string FolderUtilities::_saveStateFolderOverride = "";
string FolderUtilities::_screenshotFolderOverride = "";
vector<string> FolderUtilities::_gameFolders = vector<string>();

void FolderUtilities::SetHomeFolder(string homeFolder)
{
	_homeFolder = homeFolder;
	CreateFolder(homeFolder);
}

string FolderUtilities::GetHomeFolder()
{
	if(_homeFolder.size() == 0) {
		throw std::runtime_error("Home folder not specified");
	}
	return _homeFolder;
}

void FolderUtilities::AddKnownGameFolder(string gameFolder)
{
	bool alreadyExists = false;
	string lowerCaseFolder = gameFolder;
	std::transform(lowerCaseFolder.begin(), lowerCaseFolder.end(), lowerCaseFolder.begin(), ::tolower);

	for(string folder : _gameFolders) {
		std::transform(folder.begin(), folder.end(), folder.begin(), ::tolower);
		if(folder.compare(lowerCaseFolder) == 0) {
			alreadyExists = true;
			break;
		}
	}

	if(!alreadyExists) {
		_gameFolders.push_back(gameFolder);
	}
}

vector<string> FolderUtilities::GetKnownGameFolders()
{
	return _gameFolders;
}

void FolderUtilities::SetFolderOverrides(string saveFolder, string saveStateFolder, string screenshotFolder)
{
	_saveFolderOverride = saveFolder;
	_saveStateFolderOverride = saveStateFolder;
	_screenshotFolderOverride = screenshotFolder;
}

string FolderUtilities::GetSaveFolder()
{
	string folder;
	if(_saveFolderOverride.empty()) {
		folder = CombinePath(GetHomeFolder(), "Saves");
	} else {
		folder = _saveFolderOverride;
	}
	CreateFolder(folder);
	return folder;
}

string FolderUtilities::GetHdPackFolder()
{
	string folder = CombinePath(GetHomeFolder(), "HdPacks");
	CreateFolder(folder);
	return folder;
}

string FolderUtilities::GetDebuggerFolder()
{
	string folder = CombinePath(GetHomeFolder(), "Debugger");
	CreateFolder(folder);
	return folder;
}

string FolderUtilities::GetSaveStateFolder()
{
	string folder;
	if(_saveStateFolderOverride.empty()) {
		folder = CombinePath(GetHomeFolder(), "SaveStates");
	} else {
		folder = _saveStateFolderOverride;
	}
	CreateFolder(folder);
	return folder;
}

string FolderUtilities::GetScreenshotFolder()
{
	string folder;
	if(_screenshotFolderOverride.empty()) {
		folder = CombinePath(GetHomeFolder(), "Screenshots");
	} else {
		folder = _screenshotFolderOverride;
	}
	CreateFolder(folder);
	return folder;
}

string FolderUtilities::GetRecentGamesFolder()
{
	string folder = CombinePath(GetHomeFolder(), "RecentGames");
	CreateFolder(folder);
	return folder;
}

void FolderUtilities::CreateFolder(string folder)
{
	fs::create_directory(fs::path(folder));
}

vector<string> FolderUtilities::GetFolders(string rootFolder)
{
	vector<string> folders;

	if(!fs::is_directory(fs::path(rootFolder))) {
		return folders;
	} 
  fs::path path(rootFolder);
	for(fs::recursive_directory_iterator i(path), end; i != end; i++) {
		if(i.depth() > 1) {
			//Prevent excessive recursion
			i.disable_recursion_pending();
		} else {
			if(fs::is_directory(i->path())) {
				folders.push_back(i->path().string());
			}
		}
	}

	return folders;
}

vector<string> FolderUtilities::GetFilesInFolder(string rootFolder, std::unordered_set<string> extensions, bool recursive)
{
	vector<string> files;
	vector<string> folders = { { rootFolder } };

	if(!fs::is_directory(fs::path(rootFolder))) {
		return files;
	}

	if(recursive) {
		for(string subFolder : GetFolders(rootFolder)) {
			folders.push_back(subFolder);
		}
	}

	for(string folder : folders) {
		for(fs::directory_iterator i(fs::path(folder.c_str())), end; i != end; i++) {
			string extension = i->path().extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
			if(extensions.find(extension) != extensions.end()) {
				files.push_back(i->path().string());
			}
		}
	}

	return files;
}

string FolderUtilities::GetFilename(string filepath, bool includeExtension)
{
	fs::path filename = fs::path(filepath).filename();
	if(!includeExtension) {
		filename.replace_extension("");
	}
	return filename.string();
}

string FolderUtilities::GetFolderName(string filepath)
{
	return fs::path(filepath).remove_filename().string();
}

string FolderUtilities::CombinePath(string folder, string filename)
{
	return (fs::path(folder) / fs::path(filename)).string();
}

int64_t FolderUtilities::GetFileModificationTime(string filepath)
{
	return std::chrono::seconds(fs::last_write_time(fs::path(filepath))) / std::chrono::seconds(1);
}
