#pragma once


// 所有接口成功都返回0，失败返回错误码，‘ -1 ’ 表示‘不支持’

#define DEC_OK				(0)
#define DEC_NOT_SUPPORTED   (-1)

#define PROGRESS_INTERVAL_MS  (100)

#define SPEED_MAX   (5)   //  real speed = 2 ^ 'speed'

#define VOLUME_MAX (100)

#define DIR_FORWARD  (0)
#define DIR_BACKWARD (1)
