#include "utils.h"


CString get_path_dir(const char * full_path)
{
    const char * p = full_path + strlen(full_path);
    while ( p> full_path && '\\' != *p)
    {
        p --;
    }

    if (full_path ==  p)
    {
        return "";
    }
    
    CString s(full_path, p - full_path);

    return s;
}



int left_match(const char* short_one, const char* long_one)
{
    if (!short_one || !long_one)
    {
        return 0;
    }

    while(1)
    {
        if ( 0 == *short_one)
        {
            return 1;
        }

        if ( 0== *long_one)
        {
            return 0;
        }

        if ( *short_one !=  *long_one)
        {
            return 0;
        }

        short_one++;
        long_one++;
    }

    _ASSERT(0);
    return 0;
}


CString to_yyyymmdd_hhmmss(INT64 t)
{
    CTime ct(t);

    return ct.Format("%Y%m%d %H%M%S");
}



int CStreamDumper::create_if_need()
{
	if (real_size >= target_size)
	{
		return 1;
	}

	if (pFile)
	{
		return 0;
	}

    pFile = fopen(filepath.GetString(), "wb");
	if (!pFile)
	{
		ATLTRACE("failed to create dup file ¡®%s¡¯\n", filepath.GetString());
		return 1;
	}

	ATLTRACE("new dump at %p\n", pFile);

	
	return 0;
}


void CStreamDumper::feed_data(BYTE* buf, int buf_size)
{
	if (create_if_need())
	{
		return;
	}

	int remain = target_size - real_size;

	if (!remain)
	{
		fclose(pFile);
		pFile = 0;
		ATLTRACE("dup file ¡®%s¡¯ is created\n", filepath.GetString());

		return;
	}

	if (buf_size < remain)
	{
		fwrite(buf, 1, buf_size, pFile);
		//fflush(pFile);
		real_size += buf_size;

		ATLTRACE("got %d, now size = %d\n", buf_size, real_size);
	}
	else if (buf_size = remain)
	{
		fwrite(buf, 1, remain, pFile);
		real_size += remain;
		fclose(pFile);
		pFile = 0;

		ATLTRACE("got %d, now size = %d\n", remain, real_size);

		ATLTRACE("dump file ¡®%s¡¯ is created\n", filepath.GetString());
	}
	else if (buf_size > remain)
	{
		fwrite(buf, 1, remain, pFile);
		real_size += remain;
		fclose(pFile);
		pFile = 0;

		ATLTRACE("got %d, now size = %d\n", remain, real_size);

		ATLTRACE("dump file ¡®%s¡¯ is created\n", filepath.GetString());
	}
}


CAtlStringA get_module_dir(HMODULE hModule)
{
	char result[MAX_PATH];
	GetModuleFileNameA(hModule, result, MAX_PATH);

	char* p = result + strlen(result);
	while (p > result && '\\' != *p)
	{
		p--;
	}

	if ('\\' == *p)
	{
		*p = 0;
	}

	return result;
}
#ifdef NDEBUG
Logger      g_logger = NULL;
#else
Logger      g_logger = (Logger)printf;
#endif


SharedFile  g_log_file;

void default_logger(const char* _Format, ...);


int init_default_logger(HMODULE base_module, const char* basename)
{
	CAtlString dir;
	dir = get_module_dir(base_module);

	CAtlString filepath;
	filepath.Format("%s\\%s", dir.GetString(), basename);


	g_log_file._file = fopen(filepath.GetString(), "a");
	if (!g_log_file._file)
	{
		AtlTrace("Failed to open log file %s.\n", filepath.GetString());
		//return -1;
	}
	else
	{
		g_logger = default_logger;
	}

	return 0;

}
int printf_as_default_logger()
{
	g_log_file._file = stdout;
	g_log_file._auto_close = 0;
	g_logger = default_logger;
	return 0;
}

void uninit_default_logger()
{
	AutoLocker _lock_it(g_log_file);

	if (g_log_file._file)
	{
		fclose(g_log_file._file);
		g_log_file._file = NULL;
	}
}

void  default_logger(const char* pszFormat, ...)
{
	AutoLocker _lock_it(g_log_file);

	va_list ptr;
	va_start(ptr, pszFormat);


	vfprintf(g_log_file._file, pszFormat, ptr);
	fflush(g_log_file._file);

	va_end(ptr);


}

