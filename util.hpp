#include <sys/syscall.h>
#include <sys/mman.h>

#define PGSZ 0x1000
#define MAX_PATH 256

class Util
{
public:
    static long err;
    static int linux_write(int fd, const void *data, unsigned long len);
    static int linux_read(int fd, char *buffer, unsigned long bufferlen);
    static int linux_open(const char *pathname, unsigned long flags, unsigned long mode);
    static int linux_close(int fd);
    static int linux_stat(const char *filename, void *buf);
    static void linux_exit(int code);
    static int print_long(int fd, unsigned long i);
    static int print_hex(int fd, unsigned long i);
    static int print_string(int fd, char *s);
    static int to_decimal(unsigned long x, char *p);
    static int to_hex(unsigned long x, char *p);
    static unsigned long int strtoul(const char *nptr, char **endptr, int base);
    static void* linux_mmap(void *start, unsigned long length, int prot, int flags, int fd, unsigned long offset);
    static int linux_munmap(void *start, unsigned long length);
    static int linux_mprotect(void *addr, unsigned long len, int prot);
    static unsigned long file_size(char *filename);
    static void linux_brk(unsigned long addr);
    static unsigned long strlen(const char *s);
    static char *strchr(const char *s, int c);
    static char *strstr(const char *str, char *substr);
    static void *memcpy(void *dest, const void *src, unsigned long n);
    static char *strcat(char *dest, const char *src);
    static char *itoa(int num, char *str);
    static void error_msg(char *msg);
    static void copy_in(char *filename, void *address);
};

long Util::err = 0;

void * Util::memcpy(void *dest, const void *src, unsigned long n)
{
	unsigned long i;
	unsigned char *d = (unsigned char *)dest;
	unsigned char *s = (unsigned char *)src;

	for (i = 0; i < n; ++i)
		d[i] = s[i];

	return dest;
}

int Util::linux_write(int fd, const void *data, unsigned long len)
{
    long ret;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(__NR_write),
                   "D"(fd), "S"(data), "d"(len)
                 : "cc", "memory", "rcx",
                   "r8", "r9", "r10", "r11");
    if (ret < 0)
    {
        err = -ret;
        ret = -1;
    }
    return (int)ret;
}

int Util::linux_read(int fd, char *buffer, unsigned long bufferlen)
{

    long ret;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(__NR_read),
                   "D"(fd), "S"(buffer), "d"(bufferlen)
                 : "cc", "memory", "rcx",
                   "r8", "r9", "r10", "r11");
    if (ret < 0)
    {
        err = -ret;
        ret = -1;
    }
    return (int)ret;
}

int Util::linux_open(const char *pathname, unsigned long flags, unsigned long mode)
{

    long ret;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(__NR_open),
                   "D"(pathname), "S"(flags), "d"(mode)
                 : "cc", "memory", "rcx",
                   "r8", "r9", "r10", "r11");
    if (ret < 0)
    {
        err = -ret;
        ret = -1;
    }
    return (int)ret;
}

int Util::linux_close(int fd)
{

    long ret;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(__NR_close),
                   "D"(fd)
                 : "cc", "memory", "rcx",
                   "r8", "r9", "r10", "r11");
    if (ret < 0)
    {
        err = -ret;
        ret = -1;
    }
    return (int)ret;
}

int Util::linux_stat(const char *path, void *buf)
{
    long ret;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(4), "D"(path), "S"(buf)
                 : "memory");
    if (ret < 0)
    {
        err = -ret;
        ret = -1;
    }
    return (int)ret;
}

void Util::linux_exit(int code)
{
    asm volatile("syscall"
                 :
                 : "a"(__NR_exit), "D"(code));
}

int Util::print_long(int fd, unsigned long i)
{
    char i_buff[32];
    int l, r;
    to_decimal(i, i_buff);
    for (l = 0; i_buff[l]; ++l)
        ;
    r = linux_write(fd, i_buff, l);
    return r;
}

int Util::print_hex(int fd, unsigned long i)
{
    char i_buff[64];
    int l, r;
    to_hex(i, i_buff);
    for (l = 0; i_buff[l]; ++l)
        ;
    r = linux_write(fd, i_buff, l);
    return r;
}

int Util::print_string(int fd, char *s)
{
    int i, r;
    for (i = 0; s[i]; ++i)
        ;
    r = linux_write(fd, s, i);
    return r;
}

int Util::to_decimal(unsigned long x, char *p)
{
    int count = 0;

    if (x == 0)
        *p++ = '0';
    else
    {
        unsigned long q, b;
        int f = 0;
        b = 10000000000000000000U;

        do
        {
            q = x / b;
            if (q || f)
            {
                *p++ = ('0' + q);
                ++count;
                f = 1;
                x = x % b;
            }
            b /= 10;
        } while (b > 0);
    }

    *p = '\0';

    return count;
}

int Util::to_hex(unsigned long n, char *p)
{
    int i;
    int count = 0;
    for (i = 0; i < 16; ++i)
    {
        char x = ((n >> 60) & 0xf);
        if (x < (char)10)
            *p++ = x + '0';
        else
            *p++ = (x - 10) + 'a';
        ++count;
        n <<= 4;
    }
    *p = '\0';
    return count;
}

unsigned long int Util::strtoul(const char *nptr, char **endptr, int base)
{
    unsigned long ret = 0;
    int i;

    for (i = 0; nptr[i]; ++i)
    {
        char digit = nptr[i];
        unsigned int value;
        if (digit <= '9')
        {
            value = '0';
        }
        else if (digit <= 'Z')
        {
            value = 'A' - 10;
        }
        else if (digit <= 'z')
        {
            value = 'a' - 10;
        }
        ret *= base;
        ret += (digit - value);
        if (endptr)
            *endptr = (char*)&(nptr[i]);
    }

    return ret;
}

int Util::linux_munmap(void *start, unsigned long length)
{
    long ret;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(__NR_munmap),
                   "D"(start), "S"(length)
                 : "cc", "memory", "rcx",
                   "r8", "r9", "r10", "r11");
    if (ret < 0)
    {
        err = -ret;
        ret = -1;
    }
    return (int)ret;
}

void *Util::linux_mmap(void *start, unsigned long length, int prot, int flags, int fd, unsigned long offset)
{
    auto map = mmap(start, length, prot, flags, fd, offset);
    return (void*) map;
}

int Util::linux_mprotect(void *addr, unsigned long len, int prot)
{

    long ret;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(__NR_mprotect),
                   "D"(addr), "S"(len), "d"(prot)
                 : "cc", "memory", "rcx",
                   "r8", "r9", "r10", "r11");
    if (ret < 0)
    {
        err = -ret;
        ret = -1;
    }
    return (int)ret;
}

unsigned long Util::file_size(char *filename)
{
    char sbuf[144];
    unsigned long ret;

    if (0 > (long)(ret = linux_stat(filename, (void *)&sbuf)))
    {
        print_string(2, (char*)"stat problem: ");
        print_long(2, err);
        print_string(2, (char*)"\n");
    }
    else
    {
        ret = *(unsigned long *)(sbuf + 48);
    }

    return ret;
}

void Util::linux_brk(unsigned long addr)
{
    asm volatile("syscall"
                 :
                 : "a"(__NR_brk), "D"(addr));
}

unsigned long Util::strlen(const char *s)
{
    unsigned long r = 0;
    for (; s && *s; ++s, ++r)
        ;
    return r;
}

char *Util::strchr(const char *s, int c)
{
    char *r = nullptr;

    for (; s && *s; ++s)
    {
        if (*s == c)
        {
            r = (char *)s;
            break;
        }
    }
    return r;
}

char *Util::strcat(char *dest, const char *src)
{
    if (dest && src)
    {
        char *p = dest;
        while (*p)
            ++p;

        for (; *src; ++p, ++src)
            *p = *src;
    }

    return dest;
}
char *Util::strstr(const char *str, char *substr)
{
    char *r = nullptr;
    int substrl = strlen(substr);
    int strl = strlen(str);

    if (substrl < strl)
    {
        int i;

        for (i = 0; i <= strl - substrl; ++i)
        {
            char *p = (char *)&str[i];
            int j;

            for (j = 0; j < substrl; ++j)
            {
                if (p[j] != substr[j])
                    break;
            }

            if (j == substrl)
            {
                r = p;
                break;
            }
        }
    }
    else if (substrl == strl)
    {
        int i;
        char *p = (char *)&str[0];
        for (i = 0; i < substrl; ++i)
        {
            if (p[i] != substr[i])
                break;
        }
        if (i == substrl)
            r = p;
    }

    return r;
}

char *Util::itoa(int num, char *str)
{
    int len = 1;
    long tmp = num;
    int sign = num < 0;
    if (sign)
    {
        str[0] = '-';
        tmp = -tmp;
    }
    while (num /= 10)
        ++len;
    str[len + sign] = 0;
    while (len--)
    {
        str[len + sign] = '0' + tmp % 10;
        tmp /= 10;
    }
    return str;
}

void Util::error_msg(char *msg)
{
    char buf[32];
    print_string(1, msg);
    print_string(1, " ");
    to_decimal(err, buf);
    print_string(1, buf);
    print_string(1, "\n");
}

void Util::copy_in(char *filename, void *address)
{
	int fd, cc;
	off_t offset = 0;
	char buf[1024];

	if (0 > (fd = linux_open(filename, 0, 0)))
	{
		error_msg("opening dynamically-loaded file failed");
		linux_exit(2);
	}

	while (0 < (cc = linux_read(fd, buf, sizeof(buf))))
	{
		memcpy((address + offset), buf, cc);
		offset += cc;
	}

	linux_close(fd);
}

