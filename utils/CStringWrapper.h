#ifndef  _CSTRING__H_
#define  _CSTRING__H_

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <string>
#include <stdexcept>
#include <vector>

#ifdef __GNUC__
#include <unistd.h>
#include <iconv.h>
#define attr_printf23 __attribute__((format(printf,2,3)))
#else 
    // this is VC
#define attr_printf23
#include <malloc.h>
#endif

typedef const char * LPCTSTR;
class CString:
	public std::string
{
public:
	explicit CString(LPCTSTR pszFormat, ...) attr_printf23
	{
		va_list pArg;
		va_start(pArg, pszFormat);    

		FormatV(pszFormat, pArg);
		va_end(pArg);
	}

	CString(LPCTSTR pszFormat, va_list  pArg)
	{
		FormatV(pszFormat, pArg);
	}

	CString()
		:std::string()
	{
	}

	CString(const char * a)
		:std::string(a)
	{
	}
    
    CString(const std::string&  a)
		:std::string(a)
	{
	}
    
    CString(int i )
    {
        Format("%d",i);
    }
    
    CString(unsigned long l )
    {
        Format("%lu",l);
    }

#ifdef __GNUC__    
    CString(long long ll )
    {
        Format("%lld",ll);
    }
#endif

	CString(time_t t )
	{
		struct tm the_time;
#ifdef __GNUC__
		localtime_r(&t , &the_time);
#else
        localtime_s(&the_time , &t );
#endif
		char buf[100];
		snprintf(buf
				,sizeof(buf)-1
				, "%04u-%02u-%02u %02u:%02u:%02u"
				, the_time.tm_year + 1900
				, the_time.tm_mon + 1
				, the_time.tm_mday
				, the_time.tm_hour
				, the_time.tm_min
				, the_time.tm_sec																																		            );
		//ctime_r(&t, buf );
		this->assign(buf);
	}

	void Empty()
	{
		clear();
	}		
#ifdef _MSC_VER
	static int vasprintf(char** pbuffer, const char* format, va_list args)
    {
        int     len;
        len = _vscprintf(format, args) + 1; // _vscprintf doesn't count terminating '\0'
        *pbuffer = (char*)malloc(len );
        if (! (*pbuffer))
        {
            return -1;
        }
        vsprintf(*pbuffer, format, args); // C4996
        return len;
    }

#endif

	void Format(LPCTSTR pszFormat, ...) attr_printf23
	{
		va_list pArg;
		va_start(pArg, pszFormat);    

		char * pBuf;
		int i = vasprintf(&pBuf, pszFormat, pArg);

		if ( -1 != i)
		{

			//debug_printf("Format:  %s\n", pBuf);
			*this = (const char * )pBuf;
            free(pBuf);
		}
		else
		{
			clear();
		}

		va_end(pArg);
	}

        void format_append(LPCTSTR pszFormat, ...)  attr_printf23
        {
          va_list pArg;
          va_start(pArg, pszFormat);    

          char * pBuf;
          int i = vasprintf(&pBuf, pszFormat, pArg);

          if ( -1 != i)
          {

            //debug_printf("Format:  %s\n", pBuf);
            append(pBuf);
            free(pBuf);
          }

          va_end(pArg);
          
	}

	void FormatV(LPCTSTR pszFormat, va_list  pArg)
	{
		char * pBuf;
		int i = vasprintf(&pBuf, pszFormat, pArg);

		if ( -1 != i)
		{
			//			debug_printf("FormatV:  %s\n", pBuf);
			*this = (const char * )pBuf;
			free(pBuf);
		}
		else
		{
			clear();
		}
	}

	CString&  operator=(const char * __s)
	{ 
		this->assign(__s); 
		return *this;
	}
    
    CString&  operator=(const std::string&  __s)
	{
        if (this == & __s)
            return *this;
        
		this->assign(__s); 
		return *this;
	}

	operator LPCTSTR () const 
	{
		return c_str();
	}

    const char * GetString() const
    {
		return c_str();
    }
    
    bool start_with(const char * s) const
    {
        size_t len = strlen(s);
        if (size() < len)
        {
            return false;
        }
        
        return 0== strncmp( this->c_str() , s , len) ;
    } 

    bool end_by(const char * s) const
    {
        size_t len = strlen(s);
        if (size() < len)
        {
            return false;
        }
        
        return 0== strcmp( this->c_str() + this->size() - len , s ) ;
    }
 
    // 如果以'\n' or '\r'结尾，则删之。
    CString& remove_tail_crln()
    {
        if (this->empty())
        {
            return *this;
        }
        /* gcc++ 4.4.3 不支持 back/pop_back ...
        if ('/' == this->back())
        {
            pop_back();
        }
        */

        iterator iter = end();
        iter --;
        if ('\r' == *iter || '\n' == *iter)
        {
            erase(iter);
        }

        return *this;
    }

    // 如果以'/'结尾，则删之。
    CString& remove_tail_slash()
    {
        if (this->empty())
        {
            return *this;
        }
        /* gcc++ 4.4.3 不支持 back/pop_back ...
        if ('/' == this->back())
        {
            pop_back();
        }
        */

        iterator iter = end();
        iter --;
        if ('/' == *iter)
        {
            erase(iter);
        }

        return *this;
    }

    // 如果不以'/'结尾，则加之。
    CString& makesure_tail_slash()
    {
        /* gcc++ 4.4.3 不支持 back/pop_back ...
        if (this->empty() || '/' != this->back())
        {
            this->append("/"); 
        }
        */

        if (this->empty())
        {
            this->append("/"); 
            return *this;
        }

        iterator iter = end();
        iter --;
        if ('/' != *iter)
        {
            this->append("/"); 
        }

        return *this;
    }

    
    CString& replace_them_all(char find_this, char replace_with)
    {
        iterator iter;
        for (iter=begin(); iter!=end();iter++)
        {
            if (*iter == find_this)
                *iter = replace_with;
        }
        
        return *this;
    }
    
    CString& replace_them_all(const char * find_this, const char * replace_with)
    {
        size_t pos =0;
        size_t len = strlen(find_this);
        if (!len)
        {
            return *this;
        }
        
        size_t len2 = strlen(replace_with);
        
        while( std::string::npos !=  (pos =  this->find(find_this, pos ) )  )
        {
            this->replace(pos ,  len , replace_with );
            pos+=len2;
        }
        
        return *this;
    }
    
    //just do " -> \" , no more , no USC32 encoding!!
    CString& simple_json_str_esc()
    {
        return replace_them_all("\"", "\\\"");
    }
    
    // in mysql syntax, '\'' and '\\' are special.    
    CString& mysql_esc_with_sq(const char * to_esc)
    {
        this->clear();
        
        this->push_back('\'');        
        const char * walker;
        for (walker = to_esc; *walker; walker++)
        {
            switch (*walker)
            {
            case '\'':
                this->append("''");
                break;
            case '\\':
                this->append("\\\\");
                break;
            default:
                this->push_back(*walker);
            }
        }
        this->push_back('\'');
        return *this;
    }
    
    CString& mysql_esc_with_sq()
    {
        CString temp(*this);        
        return mysql_esc_with_sq(temp.c_str());
    }
    
    CString mysql_esc_with_sq_dup() const
    {
        CString temp(*this);        
        return temp.mysql_esc_with_sq();
    }
    
    
    CString& mysql_esc(const char * to_esc)
    {
        this->clear();
        const char * walker;
        for (walker = to_esc; *walker; walker++)
        {
            switch (*walker)
            {
            case '\'':
                this->append("''");
                break;
            case '\\':
                this->append("\\\\");
                break;
            default:
                this->push_back(*walker);
            }
        }
        return *this;
    }
    
    CString& mysql_esc()
    {
        CString temp(*this);        
        return mysql_esc(temp.c_str());
    }
    
    CString shell_arg_esc_str() const
    {
        CString temp(*this);
        return temp.shell_arg_esc();
    }

    CString& shell_arg_esc()
    {
        CString temp(*this);
        return shell_arg_esc(temp.c_str());
    }

    CString& shell_arg_esc(const char * to_esc)
    {
        this->clear();
        const char * walker;
        for (walker = to_esc; *walker; walker++)
        {
        	if ( !((*walker >= '0' && *walker <= '9') ||
        		   (*walker >= 'A' && *walker <= 'Z') ||
        		   (*walker >= 'a' && *walker <= 'z') ||
        		   *walker == '.' || *walker == ',' ))
        	{
        		this->append("\\");
        	}

        	this->push_back(*walker);
        }
        return *this;
    }

    CString& gmtime(time_t now)
    {
        this->clear();
        struct tm the_time;
#ifdef __GNUC__
        gmtime_r(&now , &the_time);
#else
        gmtime_s(&the_time , &now );
#endif
        this->Format("%04u-%02u-%02u %02u:%02u:%02u", the_time.tm_year + 1900
                , the_time.tm_mon + 1
                , the_time.tm_mday
                , the_time.tm_hour
                , the_time.tm_min
                , the_time.tm_sec);
        return *this;
    }

#ifdef __GNUC__
    time_t to_time_t()
    {
      struct tm t;
      char * ret = strptime(this->c_str(), "%Y-%m-%d %H:%M:%S", &t);
      if (!ret)
      {
        return 0;
      }
      return mktime(&t);
    }


    class   AutoCloseIconv
    {
    public:
        iconv_t me;
        AutoCloseIconv(iconv_t that)
        {
            me=that;
        }
        ~AutoCloseIconv()
        {
            if ((iconv_t)-1 != me)
            {
                iconv_close(me);
            }
        }
        
    };
    
    CString& unicode_to_utf8(const wchar_t* unicode)
    {
        // what a pity!
        // We can not use std::codecvt or boost::locale + boost::iostreams::code_coverter

        //debug_printf("unicode_to_utf8:%ls\n", unicode);

        iconv_t icd=iconv_open("UTF-8//TRANSLIT", "UCS-4LE"); // x86 is little-endian
        if ( (iconv_t)-1 == icd)
        {
            throw std::runtime_error("FailedInIconvOpen"); 
        }
        AutoCloseIconv _dont_forget(icd);

        size_t in_bytes_left = wcslen(unicode) << 2 ;
        size_t out_bytes_left = (in_bytes_left << 1) + 1 ;

        char * out_buf = (char*)alloca(out_bytes_left);
        memset( (void*)out_buf, 0 , out_bytes_left );

        char * out_buf_pointer = out_buf;

        iconv(icd
            , (char**)&unicode, &in_bytes_left
            , &out_buf_pointer ,&out_bytes_left );

        this->assign(out_buf);

		return *this;
    }
#endif // #ifdef __GNUC__    

	//如果自己是空，则返回NULL；否则返回c_str()
	const char * nullable()
	{
		return empty()? NULL : c_str();
	}

};

typedef CString CAtlStringA;
typedef CString CAtlString;

inline size_t str_split(std::vector<CString>& result, const char *str, char c = ' ')
{
    do
    {
        const char *begin = str;
        while(*str != c && *str)
            str++;

        result.push_back(std::string(begin, str));
    } while (0 != *str++);

    return result.size();
}

inline int parse_addr_port(const char * addr_port, CString& addr, int* port)
{ 
    std::vector<CString> tokens;
    size_t i = str_split(tokens, addr_port, ':');
    if (2 != i)
    {
       //throw SimpleException("Bad 'env_board_addr' in  config");
       return 1;
    }

    addr = tokens[0];
    *port = atoi(tokens[1].c_str());
    return 0;
}

inline size_t str_split2( const char *str, CString& part1, CString& part2, char c = '.')
{
    part1 = part2 = ""; 

    const char *begin = str;
    const char *end   = str + strlen(str);

    while(*str != c && *str)
        str++;

    if ( c == *str )
    {
        // we meet 'splitter'
        part1 = std::string(begin, str);

        str++;
        
        part2 = std::string(str, end);
        return 2;
    }
    else
    {
        part2 = str;
        return 1;
    }
}

#endif 

