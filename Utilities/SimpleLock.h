#pragma once 
#include "stdafx.h"

class SimpleLock
{
private:
	size_t _holderThreadID;
	uint32_t _lockCount;
	atomic_flag _lock;

public:
	SimpleLock()
	{
		_lockCount = 0;
		_holderThreadID = ~0;
	}
	void Acquire()
	{
		if(_lockCount == 0 || _holderThreadID != std::this_thread::get_id().hash()) {
			while(_lock.test_and_set());
			_holderThreadID = std::this_thread::get_id().hash();
			_lockCount = 1;
		} else {
			//Same thread can acquire the same lock multiple times
			_lockCount++;
		}
	}

	bool IsFree()
	{
		if(!_lock.test_and_set()) {
			_lock.clear();
			return true;
		}
		return false;
	}

	void WaitForRelease()
	{
		//Wait until we are able to grab a lock, and then release it again
		Acquire();
		Release();
	}

	void Release()
	{
		if(_lockCount > 0 && _holderThreadID == std::this_thread::get_id().hash()) {
			_lockCount--;
			if(_lockCount == 0) {
				_holderThreadID = ~0;
				_lock.clear();
			}
		} else {
			assert(false);
		}
	}
};
