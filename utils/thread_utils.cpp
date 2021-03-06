#include "utils.h"
#include "thread_utils.h"

void BaseThread::create_thread()
{
	_thread_handle = (HANDLE)_beginthreadex( NULL, 0
		, _thread_func
		, this
		, 0
		, &_thread_id );

	if ( ! _thread_handle )
	{
		throw SimpleException("_beginthreadex failed, errno = %d ", errno);
	}
}

unsigned  __stdcall BaseThread::_thread_func( void* arg)
{

    BaseThread* p = (BaseThread*)arg;
    
    return p->run();
}


int BaseThread::create_thread_with_cb(ThreadFunc cb, const char* thread_name, void* arg)
{
	_cb = cb;
	_arg_4_cb = arg; 

	_thread_handle = (HANDLE)_beginthreadex(NULL, 0
		, _thread_func_4_cb
		, this
		, 0
		, &_thread_id);

	if (!_thread_handle)
	{
		LOG_ERROR("_beginthreadex for %s failed, errno = %d ", thread_name, errno);
		return -1;
	}

	return 0;
}

unsigned  __stdcall BaseThread::_thread_func_4_cb(void* arg)
{
	BaseThread* p = (BaseThread*)arg;
	p->_cb(p->_arg_4_cb);
	return 0;
}



DWORD BaseThread::wait_thread_quit(DWORD  dwMilliseconds)
{
	if (!_thread_handle)
	{
		return NOTHING_TO_WAIT;
	}

	return WaitForSingleObject(_thread_handle, dwMilliseconds);
}

void BaseThread::safe_cleanup()
{
	if (!_thread_handle)
	{
		return;
	}

	CloseHandle( _thread_handle );
	_thread_handle = NULL;

}

unsigned BaseThread::run()
{
    return 0;
}

