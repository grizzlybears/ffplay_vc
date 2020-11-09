#ifndef UTILS_H_
#define UTILS_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

#include <memory>
#include <vector>
#include <list>
#include <string>
#include <map>

#include <atlstr.h>
#include <atltime.h>
#include <atltrace.h>
#include "thread_utils.h"


#ifdef _MSC_VER
#define LIKE_PRINTF23 
#define localtime_r(a,b) localtime_s(b,a)
#include <malloc.h>
#else
#define LIKE_PRINTF23 __attribute__((format(printf,2,3)))	    
#endif


typedef void (*Logger)(const char* _Format, ...);

extern Logger  g_logger;

#define LOG( format, ...) do { \
        if (!g_logger) \
        {\
            break;\
        }\
        DWORD tid = GetCurrentThreadId();\
        time_t tnow ; \
        struct tm  __now ; \
        tnow = time(NULL); \
        localtime_r(&tnow, &__now ); \
        CAtlString s; \
        s.Format(format, ## __VA_ARGS__);\
        g_logger("[%d-%d-%d %02d:%02d:%02d](%u) %s" \
                , __now.tm_year+1900 \
                , __now.tm_mon +1    \
                , __now.tm_mday      \
                , __now.tm_hour     \
                , __now.tm_min      \
                , __now.tm_sec      \
                , tid               \
                , s.GetString()     \
               );\
    } while(0)



#define LOG_DEBUG(format, ...) LOG( "Debug|%s:(%d)|"format, __FUNCTION__ , __LINE__, ## __VA_ARGS__)
#define LOG_INFO(format, ...)  LOG( "Info |%s:(%d)|"format, __FUNCTION__ , __LINE__, ## __VA_ARGS__)
#define LOG_WARN(format, ...)  LOG( "Warn |%s:(%d)|"format, __FUNCTION__ , __LINE__, ## __VA_ARGS__)
#define LOG_ERROR(format, ...) LOG( "Error|%s:(%d)|"format, __FUNCTION__ , __LINE__, ## __VA_ARGS__)


#define SIMPLE_LOG_LIBC_ERROR( where, what ) \
   do {   \
	    int code = what; \
		char* msg = strerror(code ); \
	    LOG_ERROR( "Failed  while %s, errno = %d, %s\n", where , code, msg ); \
   } while (0)


#define safe_strcpy(d , s) strncpy( (d) , (s) , sizeof(d) - 1 )

#define ARRAY_SIZE(x)  ( sizeof(x) / sizeof (x[0]) )


class SimpleException: public std::exception 
{
public:
    SimpleException(const CString& str):_mess(str) {}
    SimpleException(const std::string& str):_mess(str.c_str()) {}
    
    explicit SimpleException(const char * fmt, ...) /* __attribute__((format(printf,2,3))) */
    {
        va_list  args;
        va_start(args, fmt);

        _mess.FormatV(fmt, args);
        va_end(args);
    }

    virtual ~SimpleException() throw () {}
    virtual const char* what() const throw () {return _mess;}

protected:
    CString  _mess;
};

template <typename Drived, typename Base>
Drived * checked_cast(Base * b, Drived* d)
{
	Drived * p = dynamic_cast<Drived *> (b);
	if (!p)
	{
		throw SimpleException("Bad cast!");
	}

	return p;
}

template<typename K, typename V>
class Map2
	: public std::map<K, V>
{
public:
	typedef std::map<K, V> MyBase;
	typedef typename MyBase::iterator iterator;
	typedef typename MyBase::const_iterator const_iterator;

	bool has_key(const K& which)const
	{
		const_iterator it= typename MyBase::find(which);
		return (typename MyBase::end() != it);
	}

	bool has_key2(const K& which,iterator& the_it)
	{
		the_it= typename MyBase::find(which);
		return (typename MyBase::end() != the_it);
	}
};


//使用 insert_if_not_present 来避免插入重复元素
template<typename K, typename V>
class Multimap2
	: public std::multimap<K, V>
{
public:
	typedef std::multimap<K, V> MyBase;
	typedef typename MyBase::iterator iterator;	
	typedef typename MyBase::const_iterator const_iterator;
	typedef typename MyBase::value_type value_type;

	const_iterator find_pair( const value_type& pair) const
	{
		std::pair <const_iterator, const_iterator> gocha;
		gocha = typename MyBase::equal_range(pair.first);
		for (const_iterator it = gocha.first; it != gocha.second; ++it)
		{
			if (it->second == pair.second)
			    return it;
		}

		return typename MyBase::end();
	}

	bool insert_if_not_present( const value_type& pair)
	{
		if (find_pair( pair) == typename MyBase::end()) {
			typename MyBase::insert(pair);
			return true;
		}

		return false;
	}

	void erase_by_value(const V& v)
	{

		while (1)
		{
			iterator it;
			for (it = typename MyBase::begin(); it != typename MyBase::end() ; it++)
			{
				if ( it->second == v )
				{
					break;
				}
			}

			if (typename MyBase::end() == it)
			{
				// not found
				return;
			}

			typename MyBase::erase(it);
		}
		
	}

};


template <typename T>
class AutoReleasePtr
{
public:
	T * me;

	AutoReleasePtr( T * p = NULL)
	{
		me =p;
	}

private:
	AutoReleasePtr (const AutoReleasePtr& a )
	{
		throw "Hey! AutoReleasePtr is NOT boost::shared_ptr!\n ";
	}
public:
	AutoReleasePtr&  operator= ( T* p)
	//void assign (T* p)
	{
		if (p == me)
		{
			return *this;
		}

		release();
		me = p;

		return *this;
	}
	

	operator T* () const
	{
		return me;
	}

	T* operator->() const 
	{
		return me;
	}

	virtual ~AutoReleasePtr()
	{
		release();	
	}

	virtual void release()
	{
		if (!me)
			return;

		delete me;
		me =NULL;
	}
	
};

class AutoFree
{
public:   
	void*  me;
     AutoFree( void* p)
	{
		me = p;
	}

    ~AutoFree()
	{
		if (me)
        {
            free(me);
        }
	}
private:
    AutoFree (const AutoFree& a )
	{
		throw "Hey! AutoFree is NOT boost::shared_ptr!\n ";
	}

};


template <typename T>
class AutoDeleteArr
{
public:
	T * me;

	AutoDeleteArr( T * p = NULL)
	{
		me =p;
	}

private:
	AutoDeleteArr (const AutoDeleteArr& a )
	{
		throw "Hey! AutoDeleteArr is NOT boost::shared_ptr!\n ";
	}
public:
	AutoDeleteArr&  operator= ( T* p)
	{
		if (p == me)
		{
			return *this;
		}

		release();
		me = p;

		return *this;
	}
	

	operator T* () const
	{
		return me;
	}

	T* operator->() const 
	{
		return me;
	}

	virtual ~AutoDeleteArr()
	{
		release();	
	}

	virtual void release()
	{
		if (!me)
			return;

		delete [] me;
		me =NULL;
	}
	
};


CString get_path_dir(const char * full_path);

int left_match(const char* short_one, const char* long_one);


CString to_yyyymmdd_hhmmss(INT64 t);


//#ifdef _DEBUG

    class _PlainFuncTracer
    {
    public:
        CString _func ;
        _PlainFuncTracer(const char * func)
        {
            _func = func;
            LOG( "{{{ enter %s", func);
        }
        ~_PlainFuncTracer()
        {
            LOG( "      leave %s  }}}", _func.GetString());
        }
    };
    #define PlainFuncTracerPlease _PlainFuncTracer __dont_use_this_name_please( __FUNCTION__ )

//#else
//
//    #define PlainFuncTracerPlease 
//
//#endif


CAtlStringA get_module_dir(HMODULE hModule);

/*
	缺省logger：
		在‘base_module’所在目录下，做出文件名为‘basename’的log文件，后续log记入其中。
		若文件打开失败，则log将输出到OutputDebugString。

	上层也可以不init_default_logger，而是直接设定‘g_logger’，以自定义logger的行为。
*/
int  init_default_logger(HMODULE base_module, const char* basename);

void uninit_default_logger();

int printf_as_default_logger();

class CStreamDumper  // 用于记录回调来的数据
{
public:
	
	CStreamDumper(int size = 1024 * 1024 * 2, const char * save_as = "stream_dump.dat")
	{
		target_size = size ;
		filepath = save_as;
		pFile = NULL;
		real_size = 0;
	}
	
	void feed_data(BYTE* buf, int buf_size);

protected:
   	CString filepath;
	int   target_size;
	FILE* pFile;
	int   real_size;

	int create_if_need();

};


#endif
