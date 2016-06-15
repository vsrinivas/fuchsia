#include "__dirent.h"
#include <dirent.h>

long telldir(DIR* dir) {
    return dir->tell;
}
