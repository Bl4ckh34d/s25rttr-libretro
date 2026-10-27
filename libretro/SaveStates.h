#pragma once
#include <cstddef>
#include <boost/filesystem/path.hpp>
void ConfigureSaveStates(const boost::filesystem::path& directory);
bool SerializeGame(void* data,size_t size);
bool UnserializeGame(const void* data,size_t size);
void RunPendingStateLoad();
void ClearPendingStateLoad();
bool StateLoadPending();
