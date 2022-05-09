#include "thread_utils.h"
#include "utils.h"

void BaseThread::create_thread()
{
#ifdef __GNUC__
    int ret;
    ret = pthread_create(&_thread_handle, NULL,  trampoline, this); 
	if ( ret )
	{
		throw SimpleException(" pthread_create failed, errno = %d ", errno);
	}
#else
	_thread_handle = (HANDLE)_beginthreadex(NULL, 0
		, trampoline
		, this
		, 0
		, &_thread_id);

	if (!_thread_handle)
	{
		throw SimpleException("_beginthreadex failed, errno = %d ", errno);
	}
#endif
}

ThreadRetType STDCALL BaseThread::trampoline( void *arg)
{
    BaseThread * real_one = ( BaseThread*)arg;
    return real_one->thread_main();
}

ThreadRetType BaseThread::thread_main( )
{
    debug_printf("nothing to do.\n");
    return (ThreadRetType)NULL;
}

#define MAX_WAIT_THREAD_END_MS (20*1000)

int BaseThread::wait_thread_quit()
{
	if (!_thread_handle)
	{
		return NOTHING_TO_WAIT;
	}
#ifdef __GNUC__
	int i = pthread_join(_thread_handle, NULL);
    if (i)
    {
        SIMPLE_LOG_LIBC_ERROR( "join worder thread",   errno);
    }
    _thread_handle = 0;
	return i;

#else
	BOOL b = WaitForSingleObject(_thread_handle, MAX_WAIT_THREAD_END_MS);
    CloseHandle(_thread_handle);
	_thread_handle = NULL;
	return !b;
#endif
    
}

