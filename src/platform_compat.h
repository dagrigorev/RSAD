#ifdef _WIN32
#  include <windows.h>
inline void usleep(unsigned long us) { Sleep((DWORD)(us / 1000)); }
#  include <direct.h>
#  define mkdir(path, mode) _mkdir(path)  // mode игнорируется на Windows
#  define popen  _popen
#  define pclose _pclose
#else
#  include <sys/stat.h>
#endif