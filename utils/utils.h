#ifndef UTILS_H_
#define UTILS_H_

#include "CStringWrapper.h"
#include <assert.h>

#include <memory>
#include <vector>
#include <list>
#include <map>

#ifdef _MSC_VER
#define LIKE_PRINTF23 
#define localtime_r(a,b) localtime_s(b,a)
#include <malloc.h>
#else
#define LIKE_PRINTF23 __attribute__((format(printf,2,3)))	    
#endif

#ifdef __GNUC__
#define LOCK_LOG_FILE 
#else
#include "thread_utils.h"
extern SharedFile  g_main_logger;
#define LOCK_LOG_FILE  AutoLocker _lock_it(g_main_logger); 
#endif

#define LOG( format, ...) do { \
        time_t tnow ; \
        struct tm  __now ; \
        tnow = time(NULL); \
        localtime_r(&tnow, &__now ); \
        AString the_message; \
		the_message.Format(format, ## __VA_ARGS__); \
        LOCK_LOG_FILE; fprintf(stderr,"[%d-%d-%d %02d:%02d:%02d] %s" \
                , __now.tm_year+1900 \
                , __now.tm_mon +1    \
                , __now.tm_mday      \
                , __now.tm_hour     \
                , __now.tm_min      \
                , __now.tm_sec      \
                , the_message.GetString() \
               );   \
	    } while(0)

#define RAW_LOG( format, ...)  do { \
            LOCK_LOG_FILE; fprintf(stderr, format, ##__VA_ARGS__ ); \
	    } while(0)


#define LOG_DEBUG(format, ...) LOG( "Debug|%s:(%d)|" format, __FUNCTION__ , __LINE__, ## __VA_ARGS__)
#define LOG_INFO(format, ...)  LOG( "Info |%s:(%d)|" format, __FUNCTION__ , __LINE__, ## __VA_ARGS__)
#define LOG_WARN(format, ...)  LOG( "Warn |%s:(%s:%d)|" format, __FUNCTION__ , __FILE__, __LINE__, ## __VA_ARGS__)
#define LOG_ERROR(format, ...) LOG( "Error|%s:(%s:%d)|" format, __FUNCTION__ , __FILE__, __LINE__, ## __VA_ARGS__)


#define SIMPLE_LOG_LIBC_ERROR( where, what ) \
   do {   \
	    int code = what; \
		char* msg = strerror(code ); \
	    LOG_ERROR( "Failed  while %s, errno = %d, %s\n", where , code, msg ); \
   } while (0)

#define xstr(s) ystr(s)
#define ystr(s) #s

class SimpleException: public std::exception 
{
public:
    SimpleException(const AString& str):_mess(str) {}
    SimpleException(const std::string& str):_mess(str) {}
    
    explicit SimpleException(const char * fmt, ...) /* __attribute__((format(printf,2,3))) */
    {
        va_list  args;
        va_start(args, fmt);

        _mess.FormatV(fmt, args);
        va_end(args);
    }

    virtual ~SimpleException() throw () {}
    virtual const char* what() const throw () {return _mess.c_str();}

protected:
    AString  _mess;
};

#define LOG_THEN_THROW(format, ...) \
    do {\
        LOG_ERROR(format, ## __VA_ARGS__ );\
        throw SimpleException(format, ## __VA_ARGS__ ); \
    } while(0)


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

	bool has_key(const K& which)const
	{
        typename MyBase::const_iterator it= MyBase::find(which);
		return ( MyBase::end() != it);
	}

	bool has_key2(const K& which,typename MyBase::iterator& the_it)
	{
		the_it= MyBase::find(which);
		return ( MyBase::end() != the_it);
	}

    bool safe_remove(const K& which)
    {
        typename MyBase::iterator it =  MyBase::find(which);
        if ( MyBase::end() == it)
        {
            return false;
        }
        
        MyBase::erase(it);

        return true;
    }


    V* just_get_ptr(const K& which)
    {
        typename MyBase::iterator it= MyBase::find(which);
        if (MyBase::end() == it)
        {
            return NULL;
        }

        return & it->second;

    }
};


//使用 insert_if_not_present 来避免插入重复元素
template<typename K, typename V>
class Multimap2
	: public std::multimap<K, V>
{
public:
	typedef std::multimap<K, V> MyBase;

	typename MyBase::const_iterator find_pair( const typename MyBase::value_type& pair) const
	{
		std::pair <typename MyBase::const_iterator, typename MyBase::const_iterator> gocha;
		gocha = equal_range(pair.first);
		for (typename MyBase::const_iterator it = gocha.first; it != gocha.second; ++it)
		{
			if (it->second == pair.second)
			    return it;
		}

		return typename MyBase::end();
	}

	bool insert_if_not_present( const typename MyBase::value_type& pair)
	{
		if (find_pair( pair) == typename MyBase::end()) {
			insert(pair);
			return true;
		}

		return false;
	}

	void erase_by_value(const V& v)
	{

		while (1)
		{
			typename MyBase::iterator it;
			for (it = MyBase::begin(); it != MyBase::end() ; it++)
			{
				if ( it->second == v )
				{
					break;
				}
			}

			if (MyBase::end() == it)
			{
				// not found
				return;
			}

			erase(it);
		}
		
	}


};
#define safe_strcpy(d , s) strncpy( (d) , (s) , sizeof(d) - 1 )

#define ARRAY_SIZE(a)   ( sizeof(a) / (  sizeof (a)[0] ) )

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

    void dismiss()
    {
        me = NULL;
    }

	virtual void release()
	{
		if (!me)
			return;

		delete me;
		me =NULL;
	}
	
};

#ifdef __GNUC__
class AutoCloseFD
{
public:   
	int  me;
    AutoCloseFD( int fd)
	{
		me = fd;
	}

private:
	AutoCloseFD (const AutoCloseFD& a )
	{
		throw "Hey! AutoCloseFD is NOT boost::shared_ptr!\n ";
	}
public:
    ~AutoCloseFD()
	{
		close(me);
	}
};
#endif //#ifdef __GNUC__

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

class AutoFClose
{
public:   
	FILE*  me;
    AutoFClose( FILE* fp)
	{
		me = fp;
	}

private:
	AutoFClose (const AutoFClose& a )
	{
		throw "Hey! AutoFClose is NOT boost::shared_ptr!\n ";
	}
public:
    ~AutoFClose()
	{
		if( NULL != me )
			fclose(me);
	}
};

template <typename T>
const T* is_null( const T* x , const T* y )  
{
	return x ?  x : y ;
}


////////////////////////////////////////////////////////////////////////
/////    debug util setcion
////////////////////////////////////////////////////////////////////////

#define _CONSOLE_RESET_COLOR "\e[m"
#define _CONSOLE_MAKE_RED "\e[31m"
#define _CONSOLE_MAKE_GREEN "\e[32m"
#define _CONSOLE_MAKE_YELLOW "\e[33m"
#define _CONSOLE_BLACK_BOLD  "\e[30;1m"
#define _CONSOLE_BLUE        "\e[34m"
#define _CONSOLE_MAGENTA     "\e[35m"
#define _CONSOLE_CYAN        "\e[36m"
#define _CONSOLE_WHITE_BOLD  "\e[37;1m"

// to add more color effect,
//ref:
//  http://stackoverflow.com/questions/3506504/c-code-changes-terminal-text-color-how-to-restore-defaults-linux
//  http://www.linuxselfhelp.com/howtos/Bash-Prompt/Bash-Prompt-HOWTO-6.html
//  http://bluesock.org/~willg/dev/ansi.html

#ifndef NDEBUG
#ifdef __GNUC__
	#define debug_printf(format, ... )  \
    do { \
        int colorful_fp = isatty ( fileno(stderr));\
        fprintf( stderr, "%s%s(%d) %s (t=%lu):%s " format \
                , colorful_fp? _CONSOLE_MAKE_GREEN:""\
                ,  __FILE__, __LINE__, __func__, (unsigned long)pthread_self()  \
                ,  colorful_fp? _CONSOLE_RESET_COLOR:"" \
                ,  ##__VA_ARGS__ );\
    } while(0)

    #define debug_printf_yellow(format, ... )  \
    do { \
        int colorful_fp = isatty ( fileno(stderr));\
        fprintf( stderr, "%s(%d) %s%s: " format \
                ,  __FILE__, __LINE__, __func__ \
                , colorful_fp? _CONSOLE_MAKE_YELLOW:""\
                ,  ##__VA_ARGS__ );\
         if (colorful_fp) { fprintf( stderr, "%s", _CONSOLE_RESET_COLOR ); } \
    } while(0) 
#else
#define debug_printf LOG_DEBUG
#define debug_printf_yellow LOG_WARN
#endif

#define DEBUG_PRINT_JSON( json) \
    {\
        Json::StyledWriter  writer;\
        debug_printf("\n%s\n", writer.write( json ).c_str());\
    }

class _PlainFuncTracer
{
public:
    std::string _func;
    _PlainFuncTracer(const char* func)
    {
        _func = func;
        debug_printf("{{{  enter %s\n", _func.c_str() );
    }
    
    ~_PlainFuncTracer()
    {
        debug_printf("}}} leave %s\n\n", _func.c_str() );
    }
    
};
    #define PlainFuncTracePlease _PlainFuncTracer __dont_use_this_name( __FUNCTION__ );

#else 
	#define debug_printf(format, ...)
    #define debug_printf_yellow(format, ...)
    #define PlainFuncTracePlease 
#endif // DEBUG_TRACE


#define MakeInt64(a,b) ( (((uint64_t)(uint32_t)(a)) <<32)  | (uint32_t)b  )
#define LowInt32(a)    (  uint32_t ( (a) & 0x00000000FFFFFFFFUL)  )
#define HighInt32(a)   (  uint32_t ( ((a) & 0xFFFFFFFF00000000UL) >> 32  )  )

#define MakeInt32(a,b) ( (((uint32_t)(uint16_t)(a)) << 16)  | (uint16_t)b  )
#define LowInt16(a)    (  (uint32_t)  ( (a) & 0x0000FFFFU)  )
#define HighInt16(a)   (  (uint32_t)  ( ((a) & 0xFFFF0000U) >> 16 ))

#define TurnOnBitHigh16(who, which_bit) ( (who) |=    ( (uint32_t)( which_bit)) << 16 )
#define TurnOffBitHigh16(who, which_bit) ( (who) &=  ~( ((uint32_t)( which_bit)) << 16 ))


int32_t inline  parse_int(const char* buf, size_t buf_size)
{
#ifdef __GNUC__
    char buf2[buf_size + 1];
    buf2[buf_size]= 0;
#else
	char* buf2 = (char*)alloca(buf_size + 1);
	buf2[buf_size] = 0;
#endif

    memcpy((void*)buf2,(void*)buf, buf_size);

    return atoi(buf2);
}
#define PARSE_INT(a) parse_int( a, sizeof(a))


#ifndef ABS
#define ABS(a) ( (a) >= 0 ? (a) :  - (a) )
#endif 


//取本进程exe全路径
AString get_exe_path();

//不以‘/’结尾
AString dir_from_file_path(const char * file_path);

//取本进程exe所在目录，不以‘/’结尾
AString get_exe_dir();

// realpath 的wrapper
AString real_path(const char * path );

// aka.  /bin/bash -c ${cmd_line} 
int shell_cmd_no_wait(const char * cmd_line);

// aka.  /bin/bash -c ${cmd_line} 
// only return 0 when child process exited as '0', otherwise return nonzero or block.
int shell_cmd_wait(const char * cmd_line);

// exec shell cmd, get stdout as string
// return 0 if cmd success; return > 0 if 'cmd' fails; return < 0  if this api fails.
int shell_cmd(const char * cmd_line, AString& output );

int shell_cmd_ml(const char * cmd_line, std::vector<AString>& output );

AString put_lines_together(const std::vector<AString>& lines );
void parse_key_value_lines(const std::vector<AString>& lines, Map2<AString, AString>& dict  );
struct tm* date_add_ym(struct tm* date, int y, int m);

int get_ym_days(int y, int m); // 'm':  The number of months since January, in the range 0 to 11.

AString hex_dump(const unsigned char * buf, int buf_len, int pad_lf );

AString get_primary_mac();
AString get_primary_ip();
AString get_ip_for_target(const char * target_addr);

AString get_hostname();

//GBK转UTF8
AString GBK_to_utf8(const char* GBK);

time_t str_to_time(const char * yyyy_mm_dd_hh_mm_ss);


#ifdef _MSC_VER
#include <atlstr.h>
CAtlStringA get_module_dir(HMODULE hModule);

void    seconds_2_hms(int seconds, int* hh, int* mm, int* ss);
CAtlStringA seconds_2_hhmmss(int seconds);

unsigned int exp2(unsigned int exp);
CAtlStringA exp2(int exp);

int get_slider_max();

CAtlStringA UTF8ToMB(const CHAR* pszUtf8Text);
#endif


#endif
