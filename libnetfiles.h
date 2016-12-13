#define UNRESTRICTED 0
#define EXCLUSIVE 1
#define TRANSACTION 2
#define INVALID_FILE_MODE 999 //number chosen to not overlap with any predefined errno

int netserverinit(char *, int);
int netopen(const char *, int);
ssize_t netread(int, void *, size_t);
ssize_t netwrite(int. const void *, size_t);
int netclose(int);
