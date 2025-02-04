#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdlib.h>
#include <dirent.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>

#define PACKFS_CONCAT_(X, Y) X ## Y
#define PACKFS_CONCAT(X, Y) PACKFS_CONCAT_(X, Y)

#ifdef  PACKFS_DYNAMIC_LINKING
#define PACKFS_EXTERN(x)       (*x)
#define PACKFS_WRAP(x)         ( x)
#else
#define PACKFS_EXTERN(x) extern( x)
#define PACKFS_WRAP(x) PACKFS_CONCAT(__wrap_, x)
#endif

int                  PACKFS_EXTERN(__real_open)         (const char *path, int flags, ...);
int                  PACKFS_EXTERN(__real_openat)       (int dirfd, const char *path, int flags, ...);
int                  PACKFS_EXTERN(__real_close)        (int fd);
ssize_t              PACKFS_EXTERN(__real_read)         (int fd, void* buf, size_t count);
int                  PACKFS_EXTERN(__real_access)       (const char *path, int flags);
off_t                PACKFS_EXTERN(__real_lseek)        (int fd, off_t offset, int whence);
int                  PACKFS_EXTERN(__real_stat)         (const char *restrict path, struct stat *restrict statbuf);
int                  PACKFS_EXTERN(__real_fstat)        (int fd, struct stat * statbuf);
int                  PACKFS_EXTERN(__real_fstatat)      (int dirfd, const char* path, struct stat * statbuf, int flags);
int                  PACKFS_EXTERN(__real_statx)        (int dirfd, const char *restrict path, int flags, unsigned int mask, struct statx *restrict statbuf);
FILE*                PACKFS_EXTERN(__real_fopen)        (const char *path, const char *mode);
int                  PACKFS_EXTERN(__real_fclose)       (FILE* stream);
int                  PACKFS_EXTERN(__real_fileno)       (FILE* stream);
int                  PACKFS_EXTERN(__real_fcntl)        (int fd, int action, ...);
DIR*                 PACKFS_EXTERN(__real_opendir)      (const char *path);
DIR*                 PACKFS_EXTERN(__real_fdopendir)    (int dirfd);
int                  PACKFS_EXTERN(__real_closedir)     (DIR *dirp);
struct dirent*       PACKFS_EXTERN(__real_readdir)      (DIR *dirp);

void packfs_init__real()
{
#ifdef PACKFS_DYNAMIC_LINKING
    #include <dlfcn.h>
    __real_open      = dlsym(RTLD_NEXT, "open");
    __real_openat    = dlsym(RTLD_NEXT, "openat");
    __real_close     = dlsym(RTLD_NEXT, "close");
    __real_read      = dlsym(RTLD_NEXT, "read");
    __real_access    = dlsym(RTLD_NEXT, "access");
    __real_lseek     = dlsym(RTLD_NEXT, "lseek");
    __real_stat      = dlsym(RTLD_NEXT, "stat");
    __real_fstat     = dlsym(RTLD_NEXT, "fstat");
    __real_fstatat   = dlsym(RTLD_NEXT, "fstatat");
    __real_statx     = dlsym(RTLD_NEXT, "statx");
    __real_fopen     = dlsym(RTLD_NEXT, "fopen");
    __real_fclose    = dlsym(RTLD_NEXT, "fclose");
    __real_fileno    = dlsym(RTLD_NEXT, "fileno");
    __real_fcntl     = dlsym(RTLD_NEXT, "fcntl");
    __real_opendir   = dlsym(RTLD_NEXT, "opendir");
    __real_fdopendir = dlsym(RTLD_NEXT, "fdopendir");
    __real_closedir  = dlsym(RTLD_NEXT, "closedir");
    __real_readdir   = dlsym(RTLD_NEXT, "readdir");
#endif
}
    
#include "packfsutils.h"

enum
{
    packfs_filefd_min = 1000000000, 
    packfs_filefd_max = 1000008192, 
    packfs_entries_name_maxlen = 128, 
    packfs_archive_entries_nummax = 8192,
};

// TODO: unify packfs_open and packfs_opendir
// TODO: unify builtin and archive lists
// TODO: support working with linked zip

int packfs_initialized, packfs_enabled;
int packfs_filefd          [packfs_filefd_max - packfs_filefd_min];
int packfs_filefdrefs      [packfs_filefd_max - packfs_filefd_min];
char packfs_fileisdir      [packfs_filefd_max - packfs_filefd_min];
void* packfs_fileptr       [packfs_filefd_max - packfs_filefd_min];
size_t packfs_filesize     [packfs_filefd_max - packfs_filefd_min];
size_t packfs_fileino      [packfs_filefd_max - packfs_filefd_min];
struct dirent packfs_dirent[packfs_filefd_max - packfs_filefd_min];

size_t packfs_builtin_enries_num;
const char** packfs_builtin_entries_names; 
const char** packfs_builtin_starts; 
const char** packfs_builtin_ends;

size_t packfs_archive_entries_num;
char   packfs_archive_prefix[packfs_archive_entries_nummax * packfs_entries_name_maxlen];
size_t packfs_archive_entries_sizes[packfs_archive_entries_nummax];
char   packfs_archive_entries_isdir[packfs_archive_entries_nummax];
char   packfs_archive_entries_names[packfs_archive_entries_nummax * packfs_entries_name_maxlen];
size_t packfs_archive_entries_names_lens[packfs_archive_entries_nummax];
size_t packfs_archive_entries_names_total;
char   packfs_archive_entries_prefix[packfs_archive_entries_nummax * packfs_entries_name_maxlen];
size_t packfs_archive_entries_prefix_lens[packfs_archive_entries_nummax];
size_t packfs_archive_entries_prefix_total;
char   packfs_archive_entries_archive[packfs_archive_entries_nummax * packfs_entries_name_maxlen];
size_t packfs_archive_entries_archive_lens[packfs_archive_entries_nummax];
size_t packfs_archive_entries_archive_total;

///////////

#include <archive.h>
#include <archive_entry.h>

const char* packfs_archive_read_new(struct archive* a)
{
    static char packfs_archive_suffix[] = ".iso:.zip:.tar:.tar.gz:.tar.xz";
    if(a != NULL)
    {
        archive_read_support_format_iso9660(a);
        archive_read_support_format_zip(a);
        archive_read_support_format_tar(a);
        archive_read_support_filter_gzip(a);
        archive_read_support_filter_xz(a);
    }
    return packfs_archive_suffix;
}

void packfs_scan_archive(const char* packfs_archive_filename, const char* prefix) // for every entry need to store index into a list of archives and index into a list of prefixes
{
    //FIXME: adds prefix even if input archive cannot be opened
    //FIXME: do not scan the same archive second time
    if(packfs_archive_prefix[0] == '\0')
    {
        strcpy(packfs_archive_prefix, prefix);
    }
    else
    {
        const char pathsep[] = {packfs_pathsep, '\0'};
        strcat(packfs_archive_prefix, pathsep);
        strcat(packfs_archive_prefix, prefix);
    }

    size_t prefix_len = prefix != NULL ? strlen(prefix) : 0;
    if(prefix_len > 0 && prefix[prefix_len - 1] == packfs_sep) prefix_len--;
    size_t packfs_archive_filename_len = strlen(packfs_archive_filename);

    struct archive *a = archive_read_new(); packfs_archive_read_new(a);
    struct archive_entry *entry;
    FILE* packfs_archive_fileptr = NULL;
    do
    {
        if( packfs_archive_filename == NULL || 0 == strlen(packfs_archive_filename))
            break;

        packfs_archive_fileptr = __real_fopen(packfs_archive_filename, "rb");
        if(packfs_archive_fileptr == NULL) break;
        
        if(archive_read_open_FILE(a, packfs_archive_fileptr) != ARCHIVE_OK)
            break;
        
        //if(archive_read_open1(a) != ARCHIVE_OK)
        //    break;
        
        packfs_archive_entries_isdir[packfs_archive_entries_num] = 1;

        
        strcpy(packfs_archive_entries_names + packfs_archive_entries_names_total, "");
        packfs_archive_entries_names_lens[packfs_archive_entries_num] = 0;
        packfs_archive_entries_names_total += packfs_archive_entries_names_lens[packfs_archive_entries_num] + 1;
        
        
        strncpy(packfs_archive_entries_prefix + packfs_archive_entries_prefix_total, prefix, prefix_len);
        packfs_archive_entries_prefix_lens[packfs_archive_entries_num] = prefix_len;
        packfs_archive_entries_prefix_total += packfs_archive_entries_prefix_lens[packfs_archive_entries_num] + 1;
        
        
        strncpy(packfs_archive_entries_archive + packfs_archive_entries_archive_total, packfs_archive_filename, packfs_archive_filename_len);
        packfs_archive_entries_archive_lens[packfs_archive_entries_num] = packfs_archive_filename_len;
        packfs_archive_entries_archive_total += packfs_archive_entries_archive_lens[packfs_archive_entries_num] + 1;
        

        packfs_archive_entries_num++;
        
        while(1)
        {
            int r = archive_read_next_header(a, &entry);
            if (r == ARCHIVE_EOF)
                break;
            if (r != ARCHIVE_OK)
                break; //fprintf(stderr, "%s\n", archive_error_string(a));
                
            int filetype = archive_entry_filetype(entry);
            size_t entry_byte_size = (size_t)archive_entry_size(entry);
            const char* entryname = archive_entry_pathname(entry);
            size_t entryname_len = strlen(entryname);
            if(entryname_len > 0 && entryname[entryname_len - 1] == packfs_sep) entryname_len--;
    
            packfs_archive_entries_isdir[packfs_archive_entries_num] = filetype == AE_IFDIR;
            packfs_archive_entries_sizes[packfs_archive_entries_num] = entry_byte_size;
            
            
            strncpy(packfs_archive_entries_names + packfs_archive_entries_names_total, entryname, entryname_len);
            packfs_archive_entries_names_lens[packfs_archive_entries_num] = entryname_len;
            packfs_archive_entries_names_total += packfs_archive_entries_names_lens[packfs_archive_entries_num] + 1;
        
            
            strncpy(packfs_archive_entries_prefix + packfs_archive_entries_prefix_total, prefix, prefix_len);
            packfs_archive_entries_prefix_lens[packfs_archive_entries_num] = prefix_len;
            packfs_archive_entries_prefix_total += packfs_archive_entries_prefix_lens[packfs_archive_entries_num] + 1;
        
            
            strncpy(packfs_archive_entries_archive + packfs_archive_entries_archive_total, packfs_archive_filename, packfs_archive_filename_len);
            packfs_archive_entries_archive_lens[packfs_archive_entries_num] = packfs_archive_filename_len;
            packfs_archive_entries_archive_total += packfs_archive_entries_archive_lens[packfs_archive_entries_num] + 1;

            packfs_archive_entries_num++;
                
            r = archive_read_data_skip(a);
            if (r == ARCHIVE_EOF)
                break;
            if (r != ARCHIVE_OK)
                break; //fprintf(stderr, "%s\n", archive_error_string(a));
        }
    }
    while(0);
    archive_read_close(a);
    archive_read_free(a);
    if(packfs_archive_fileptr != NULL) __real_fclose(packfs_archive_fileptr);
}

void packfs_extract_archive_entry(const char* packfs_archive_prefix, const char* entrypath, FILE* fileptr)
{
    struct archive *a = archive_read_new(); packfs_archive_read_new(a);
    struct archive_entry *entry;
    FILE* packfs_archive_fileptr = __real_fopen(packfs_archive_prefix, "rb");//packfs_archive_fileptr;
    do
    {
        //fseek(packfs_archive_fileptr, 0, SEEK_SET);
        if(packfs_archive_fileptr == NULL) break;
        if(archive_read_open_FILE(a, packfs_archive_fileptr) != ARCHIVE_OK)
            break;
        
        while (1)
        {
            int r = archive_read_next_header(a, &entry);
            if (r == ARCHIVE_EOF)
                break;
            if (r != ARCHIVE_OK)
                break; //fprintf(stderr, "%s\n", archive_error_string(a));

            if(0 == strcmp(entrypath, archive_entry_pathname(entry)))
            {
                enum { MAX_WRITE = 1024 * 1024};
                const void *buff;
                size_t size;
                off_t offset;

                while ((r = archive_read_data_block(a, &buff, &size, &offset)) == ARCHIVE_OK)
                {
                    // assert(offset <= output_offset), do not support sparse files just yet, https://github.com/libarchive/libarchive/issues/2299
                    const char* p = buff;
                    while (size > 0)
                    {
                        ssize_t bytes_written = fwrite(p, 1, size, fileptr);
                        p += bytes_written;
                        size -= bytes_written;
                    }
                }
                break;
            }
            else
            {
                r = archive_read_data_skip(a);
                if (r == ARCHIVE_EOF)
                    break;
                if (r != ARCHIVE_OK)
                    break; //fprintf(stderr, "%s\n", archive_error_string(a));
            }
        }
    }
    while(0);
    archive_read_close(a);
    archive_read_free(a);
    if(packfs_archive_fileptr != NULL) __real_fclose(packfs_archive_fileptr);
    
}

///////////

void packfs_init(const char* path)
{ 
    if(packfs_initialized != 1)
    {
        packfs_init__real();
        packfs_initialized = 1;
        packfs_enabled = 0;
    }

    if(packfs_initialized == 1 && packfs_enabled == 0)
    {
        char path_normalized[packfs_entries_name_maxlen]; 
        
        const char *packfs_archives = getenv("PACKFS_ARCHIVES");
        const char* packfs_archives_suffixes = packfs_archive_read_new(NULL);
        
        if(packfs_archives != NULL && packfs_archives[0] != '\0')
        {
            for(const char* begin = packfs_archives, *end = strchr(packfs_archives, packfs_pathsep), *prevend  = packfs_archives; prevend != NULL && *begin != '\0'; prevend = end, begin = (end + 1), end = end != NULL ? strchr(end + 1, packfs_pathsep) : NULL)
            {
                size_t len = end == NULL ? strlen(begin) : (end - begin);
                strncpy(path_normalized, begin, len);
                path_normalized[len] = '\0';
                
                char* a = strchr(path_normalized, '@');
                const char* prefix = a != NULL ? (a + 1) : "/packfs";
                path_normalized[a != NULL ? (a - begin) : len] = '\0';
                
                packfs_enabled = 1;
                packfs_scan_archive(path_normalized, prefix);
            }
        }
        else if(path != NULL)
        {
            packfs_normalize_path(path_normalized, path);
            size_t path_prefix_len = packfs_archive_prefix_extract(path_normalized, packfs_archives_suffixes);
            if(path_prefix_len > 0)
            {
                path_normalized[path_prefix_len] = '\0';
                const char* prefix = path_normalized;
                
                packfs_enabled = 1;
                packfs_scan_archive(path_normalized, prefix);
            }
        }
    }
}

int packfs_fd_in_range(int fd)
{
    return fd >= 0 && fd >= packfs_filefd_min && fd < packfs_filefd_max;
}

void* packfs_find(int fd, void* ptr)
{
    if(ptr != NULL)
    {
        for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
        {
            if(packfs_fileptr[k] == ptr)
                return &packfs_filefd[k];
        }
        return NULL;
    }
    else
    {
        if(!packfs_fd_in_range(fd))
            return NULL;
        
        for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
        {
            if(packfs_filefd[k] == fd)
                return packfs_fileptr[k];
        }
    }
    return NULL;
}

void packfs_resolve_relative_path(char* dest, int dirfd, const char* path)
{
    size_t K = 0, found = 0;
    for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
    {
        if(packfs_filefd[k] == dirfd)
        {
            K = packfs_fileino[k];
            found = 1;
            break;
        }
    }

    for(size_t i = 0, packfs_archive_entries_names_offset = 0, packfs_archive_entries_prefix_offset = 0; packfs_enabled && packfs_initialized && found && i < packfs_archive_entries_num; packfs_archive_entries_names_offset += (packfs_archive_entries_names_lens[i] + 1), packfs_archive_entries_prefix_offset += (packfs_archive_entries_prefix_lens[i] + 1), i++)
    {
        if(i == K)
        {
            const char* prefix    = packfs_archive_entries_prefix + packfs_archive_entries_prefix_offset;
            const char* entrypath = packfs_archive_entries_names  + packfs_archive_entries_names_offset;
            entrypath = (strlen(entrypath) > 1 && entrypath[0] == '.' && entrypath[1] == packfs_sep) ? (entrypath + 2) : entrypath;
            path = (strlen(path) > 1 && path[0] == '.' && path[1] == packfs_sep) ? (path + 2) : path;
            if(strlen(entrypath) > 0)
                sprintf(dest, "%s%c%s%c%s", prefix, (char)packfs_sep, entrypath, (char)packfs_sep, path);
            else
                sprintf(dest, "%s%c%s", prefix, (char)packfs_sep, path);
            return;
        }
    }

    strcpy(dest, path);
}

struct dirent* packfs_readdir(void* stream)
{
    struct dirent* dir_entry = stream;
    for(size_t i = 0, packfs_archive_entries_names_offset = 0; i < packfs_archive_entries_num; packfs_archive_entries_names_offset += (packfs_archive_entries_names_lens[i] + 1), i++)
    {
        const char* path = packfs_archive_entries_names + packfs_archive_entries_names_offset;
        const char* dir_entry_name = packfs_archive_entries_names + (size_t)dir_entry->d_off;
        
        if(i > (size_t)dir_entry->d_ino && packfs_indir(dir_entry_name, path))
        {
            dir_entry->d_type = packfs_archive_entries_isdir[i] ? DT_DIR : DT_REG;
            strcpy(dir_entry->d_name, packfs_basename(path));
            dir_entry->d_ino = (ino_t)i;
            return dir_entry;
        }
    }
    return NULL;
}


int packfs_access(const char* path)
{
    char path_normalized[packfs_entries_name_maxlen]; packfs_normalize_path(path_normalized, path);
    if(packfs_path_in_range(packfs_archive_prefix, path_normalized))
    {
        for(size_t i = 0; i < packfs_builtin_enries_num; i++)
        {
            if(0 == strcmp(path_normalized, packfs_builtin_entries_names[i]))
                return 0;
        }
        
        for(size_t i = 0, packfs_archive_entries_names_offset = 0, packfs_archive_entries_prefix_offset = 0; i < packfs_archive_entries_num; packfs_archive_entries_names_offset += (packfs_archive_entries_names_lens[i] + 1), packfs_archive_entries_prefix_offset += (packfs_archive_entries_prefix_lens[i] + 1), i++)
        {
            const char* prefix     = packfs_archive_entries_prefix + packfs_archive_entries_prefix_offset;
            const char* entrypath = packfs_archive_entries_names  + packfs_archive_entries_names_offset;
            if(!packfs_archive_entries_isdir[i] && packfs_match(path_normalized, prefix, entrypath))
                return 0;
        }
        return -1;
    }
    return -2;
}

int packfs_stat(const char* path, int fd, size_t* isdir, size_t* size, size_t* d_ino)
{
    char path_normalized[packfs_entries_name_maxlen]; packfs_normalize_path(path_normalized, path);
    
    if(packfs_path_in_range(packfs_archive_prefix, path_normalized))
    {
        for(size_t i = 0; i < packfs_builtin_enries_num; i++)
        {
            const char* entrypath = packfs_builtin_entries_names[i];
            int entryisdir = entrypath[0] != '\0' && entrypath[strlen(entrypath) - 1] == packfs_sep;
            if(0 == strcmp(path_normalized, entrypath))
            {
                *size = (off_t)(packfs_builtin_ends[i] - packfs_builtin_starts[i]);
                *isdir = entryisdir;
                *d_ino = i;
                return 0;
            }
        }
        
        /*
        for(size_t i = 0; i < packfs_builtin_dirs_num; i++)
        {
            if(0 == strcmp(path_normalized, packfs_builtin_entries_names_dirs[i]))
            {
                *size = 0;
                *isdir = 1;
                *d_ino = i; // FIXME: need to unite builtin and archive list?, otherwise d_ino needs to know in which list to address
                return 0;
            }
        }
        */
        
        for(size_t i = 0, packfs_archive_entries_names_offset = 0, packfs_archive_entries_prefix_offset = 0; i < packfs_archive_entries_num; packfs_archive_entries_names_offset += (packfs_archive_entries_names_lens[i] + 1), packfs_archive_entries_prefix_offset += (packfs_archive_entries_prefix_lens[i] + 1), i++)
        {
            const char* prefix     = packfs_archive_entries_prefix + packfs_archive_entries_prefix_offset;
            const char* entrypath = packfs_archive_entries_names  + packfs_archive_entries_names_offset;
            if(packfs_match(path_normalized, prefix, entrypath))
            {
                *size = packfs_archive_entries_sizes[i];
                *isdir = packfs_archive_entries_isdir[i];
                *d_ino = i;
                return 0;
            }
        }
        return -1;
    }
    
    if(packfs_fd_in_range(fd))
    {
        for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
        {
            if(packfs_filefd[k] == fd)
            {
                *size = packfs_filesize[k];
                *isdir = packfs_fileisdir[k];
                *d_ino = packfs_fileino[k];
                return 0;
            }
        }
        return -1;
    }

    return -2;
}

void* packfs_open(const char* path)
{
    char path_normalized[packfs_entries_name_maxlen]; packfs_normalize_path(path_normalized, path);

    void* fileptr = NULL; size_t filesize = 0, fileino = 0, d_ino = 0, d_off = 0, found = 0;
    
    if(packfs_path_in_range(packfs_archive_prefix, path_normalized))
    {
        for(size_t i = 0; i < packfs_builtin_enries_num; i++)
        {
            if(0 == strcmp(path_normalized, packfs_builtin_entries_names[i]))
            {
                filesize = (size_t)(packfs_builtin_ends[i] - packfs_builtin_starts[i]);
                fileptr = fmemopen((void*)packfs_builtin_starts[i], filesize, "r");
                break;
            }
        }
        
        for(size_t i = 0, packfs_archive_entries_names_offset = 0, packfs_archive_entries_prefix_offset = 0; i < packfs_archive_entries_num; packfs_archive_entries_names_offset += (packfs_archive_entries_names_lens[i] + 1), packfs_archive_entries_prefix_offset += (packfs_archive_entries_prefix_lens[i] + 1), i++)
        {
            const char* prefix    = packfs_archive_entries_prefix + packfs_archive_entries_prefix_offset;
            const char* entrypath = packfs_archive_entries_names  + packfs_archive_entries_names_offset;
            if(packfs_archive_entries_isdir[i] && packfs_match(path_normalized, prefix, entrypath))
            {
                d_ino = i;
                d_off = packfs_archive_entries_names_offset;
                found = 1;
                break;
            }
        }
        
        for(size_t i = 0, packfs_archive_entries_names_offset = 0, packfs_archive_entries_prefix_offset = 0, packfs_archive_entries_archive_offset = 0; i < packfs_archive_entries_num; packfs_archive_entries_names_offset += (packfs_archive_entries_names_lens[i] + 1), packfs_archive_entries_prefix_offset += (packfs_archive_entries_prefix_lens[i] + 1), packfs_archive_entries_archive_offset += (packfs_archive_entries_archive_lens[i] + 1), i++)
        {
            const char* prefix    = packfs_archive_entries_prefix + packfs_archive_entries_prefix_offset;
            const char* entrypath = packfs_archive_entries_names  + packfs_archive_entries_names_offset;
            const char* archive   = packfs_archive_entries_archive+ packfs_archive_entries_archive_offset;
            
            if(!packfs_archive_entries_isdir[i] && packfs_match(path_normalized, prefix, entrypath))
            {
                fileino = i;
                filesize = packfs_archive_entries_sizes[i];
                fileptr = fmemopen(NULL, filesize, "rb+");
                packfs_extract_archive_entry(archive, entrypath, (FILE*)fileptr);
                fseek((FILE*)fileptr, 0, SEEK_SET);
                break;
            }
        }
    }
    
    for(size_t k = 0; fileptr != NULL && k < packfs_filefd_max - packfs_filefd_min; k++)
    {
        if(packfs_filefd[k] == 0)
        {
            /*
            fileptr = &packfs_dirent[k];
            *((struct dirent*)fileptr) = (struct dirent){0};
             ((struct dirent*)fileptr)->d_ino = (ino_t)d_ino;
             ((struct dirent*)fileptr)->d_off = (off_t)d_off;
            */
            int fd = packfs_filefd_min + k;

            packfs_fileisdir[k] = 0; // packfs_fileisdir[k] = 1;
            packfs_filefdrefs[k] = 1;
            packfs_filefd[k] = fd;
            packfs_fileptr[k] = fileptr;
            packfs_filesize[k] = filesize; // packfs_filesize[k] = 0;
            packfs_fileino[k] = fileino; // packfs_fileino[k] = d_ino;
            return fileptr;
        }
    }

    return NULL;
}

int packfs_close(int fd)
{
    if(!packfs_fd_in_range(fd))
        return -2;

    for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
    {
        if(packfs_filefd[k] == fd)
        {
            packfs_filefdrefs[k]--;
            if(packfs_filefdrefs[k] > 0)
                return 0;

            int res = (!packfs_fileisdir[k]) ? __real_fclose(packfs_fileptr[k]) : 0;
            packfs_dirent[k]  = (struct dirent){0};
            packfs_fileisdir[k] = 0;
            packfs_filefd[k] = 0;
            packfs_filesize[k] = 0;
            packfs_fileptr[k] = NULL;
            packfs_fileino[k] = 0;
            return res;
        }
    }
    return -1;
}

ssize_t packfs_read(int fd, void* buf, size_t count)
{
    FILE* ptr = packfs_find(fd, NULL);
    if(!ptr)
        return -1;
    return fread(buf, 1, count, ptr);
}

int packfs_seek(int fd, long offset, int whence)
{
    FILE* ptr = packfs_find(fd, NULL);
    if(!ptr)
        return -1;
    return fseek(ptr, offset, whence);
}

int packfs_dup(int oldfd, int newfd)
{
    int K = -1;
    if(oldfd >= 0 && packfs_filefd_min <= oldfd && oldfd < packfs_filefd_max)
    {
        for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
        {
            if(packfs_filefd[k] == oldfd)
            {
                K = k;
                break;
            }
        }
    }
    for(size_t k = 0; K >= 0 && k < packfs_filefd_max - packfs_filefd_min; k++)
    {
        int fd = packfs_filefd_min + k;
        if(packfs_filefd[k] == 0 && (newfd < packfs_filefd_min || newfd >= fd))
        {
            packfs_filefdrefs[K]++;
            
            packfs_fileisdir[k] = packfs_fileisdir[K];
            packfs_filefd[k]    = fd;
            packfs_filefdrefs[k]= 1;
            packfs_filesize[k]  = packfs_filesize[K];
            packfs_fileino[k]   = packfs_fileino[K];
            packfs_dirent[k]    = packfs_dirent[K];
            packfs_fileptr[k]   = packfs_fileptr[K];
            return fd;
        }
    }
    return -1;
    
}

///////////

FILE* PACKFS_WRAP(fopen)(const char *path, const char *mode)
{
    packfs_init(path);
    if(packfs_enabled && packfs_path_in_range(packfs_archive_prefix, path))
    {
        FILE* res = packfs_open(path);
        if(res != NULL)
            return res;
    }
    return __real_fopen(path, mode);
}

int PACKFS_WRAP(fileno)(FILE *stream)
{
    packfs_init(NULL);
    int res = __real_fileno(stream);
    if(packfs_enabled && res < 0)
    {        
        int* ptr = packfs_find(-1, stream);
        res = ptr == NULL ? -1 : (*ptr);
    }
    return res;
}

int PACKFS_WRAP(fclose)(FILE* stream)
{
    packfs_init(NULL);
    if(packfs_enabled && packfs_find(-1, stream) != NULL)
    {
        int* ptr = packfs_find(-1, stream);
        int fd = ptr == NULL ? -1 : *ptr;
        int res = packfs_close(fd);
        if(res >= -1)
            return res;
    }
    return __real_fclose(stream);
}

int PACKFS_WRAP(open)(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if((flags & O_CREAT) != 0 || (flags & O_TMPFILE) != 0)
    {
        va_list arg;
        va_start(arg, flags);
        mode = va_arg(arg, mode_t);
        va_end(arg);
    }
    
    packfs_init(path);
    if(packfs_enabled && packfs_path_in_range(packfs_archive_prefix, path))
    {
        void* stream = packfs_open(path); //((flags & O_DIRECTORY) != 0) ? (void*)packfs_opendir(path) : (void*)packfs_open(path);
        if(stream != NULL)
        {
            int* ptr = packfs_find(-1, stream);
            int res = ptr == NULL ? -1 : (*ptr);
            return res;
        }
    }

    return __real_open(path, flags, mode);
}

int PACKFS_WRAP(openat)(int dirfd, const char *path, int flags, ...)
{
    mode_t mode = 0;
    if((flags & O_CREAT) != 0 || (flags & O_TMPFILE) != 0)
    {
        va_list arg;
        va_start(arg, flags);
        mode = va_arg(arg, mode_t);
        va_end(arg);
    }

    packfs_init(path);
    char path_normalized[packfs_entries_name_maxlen]; packfs_resolve_relative_path(path_normalized, dirfd, path);
    if(packfs_enabled && packfs_path_in_range(packfs_archive_prefix, path_normalized))
    {
        void* stream = packfs_open(path_normalized); // ((flags & O_DIRECTORY) != 0) ? (void*)packfs_opendir(path_normalized) : (void*)packfs_open(path_normalized);
        if(stream != NULL)
        {
            int* ptr = packfs_find(-1, stream);
            int res = ptr == NULL ? -1 : (*ptr);
            return res;
        }
    }
    
    return __real_openat(dirfd, path, flags, mode);
}

int PACKFS_WRAP(close)(int fd)
{
    packfs_init(NULL);
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        int res = packfs_close(fd);
        if(res >= -1)
            return res;
    }
    return __real_close(fd);
}

ssize_t PACKFS_WRAP(read)(int fd, void* buf, size_t count)
{
    packfs_init(NULL);
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        ssize_t res = packfs_read(fd, buf, count);
        if(res >= 0)
            return res;
    }
    return __real_read(fd, buf, count);
}

off_t PACKFS_WRAP(lseek)(int fd, off_t offset, int whence)
{
    packfs_init(NULL);
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        int res = packfs_seek(fd, (long)offset, whence);
        if(res >= 0)
            return res;
    }
    return __real_lseek(fd, offset, whence);
}

int PACKFS_WRAP(access)(const char *path, int flags)
{
    packfs_init(path);
    if(packfs_enabled && packfs_path_in_range(packfs_archive_prefix, path))
    {
        int res = packfs_access(path);
        if(res >= -1)
            return res;
    }
    return __real_access(path, flags); 
}

int PACKFS_WRAP(stat)(const char *restrict path, struct stat *restrict statbuf)
{
    packfs_init(path);
    if(packfs_enabled && packfs_path_in_range(packfs_archive_prefix, path))
    {
        *statbuf = (struct stat){0};
        size_t size = 0, isdir = 0, d_ino = 0;
        int res = packfs_stat(path, -1, &isdir, &size, &d_ino);
        if(res == 0)
        {
            statbuf->st_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->st_size = size;
            statbuf->st_ino = (ino_t)d_ino;
        }
        if(res >= -1)
            return res;
    }

    return __real_stat(path, statbuf);
}

int PACKFS_WRAP(fstat)(int fd, struct stat * statbuf)
{
    packfs_init(NULL);
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        *statbuf = (struct stat){0};
        size_t size = 0, isdir = 0, d_ino = 0;
        int res = packfs_stat(NULL, fd, &isdir, &size, &d_ino);
        if(res == 0)
        {
            statbuf->st_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->st_size = size;
            statbuf->st_ino = (ino_t)d_ino;
        }
        if(res >= -1)
            return res;
    }
    
    return __real_fstat(fd, statbuf);
}

int PACKFS_WRAP(fstatat)(int dirfd, const char* path, struct stat * statbuf, int flags)
{
    packfs_init(path);
    char path_normalized[packfs_entries_name_maxlen]; packfs_resolve_relative_path(path_normalized, dirfd, path);
    if(packfs_enabled && packfs_path_in_range(packfs_archive_prefix, path_normalized))
    {
        *statbuf = (struct stat){0};
        size_t size = 0, isdir = 0, d_ino = 0;
        int res = packfs_stat(path_normalized, -1, &isdir, &size, &d_ino);
        if(res == 0)
        {
            statbuf->st_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->st_size = size;
            statbuf->st_ino = (ino_t)d_ino;
        }

        if(res >= -1)
            return res;
    }
    
    return __real_fstatat(dirfd, path, statbuf, flags);
}

int PACKFS_WRAP(statx)(int dirfd, const char *restrict path, int flags, unsigned int mask, struct statx *restrict statbuf)
{
    packfs_init(path);
    char path_normalized[packfs_entries_name_maxlen]; packfs_resolve_relative_path(path_normalized, dirfd, path);

    if(packfs_enabled && packfs_path_in_range(packfs_archive_prefix, path_normalized))
    {
        size_t size = 0, isdir = 0, d_ino = 0;
        int res = packfs_stat(path_normalized, -1, &isdir, &size, &d_ino);
        if(res == 0)
        {
            *statbuf = (struct statx){0};
            statbuf->stx_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->stx_size = size;
            statbuf->stx_ino = d_ino;
        }
        return res;
    }

    return __real_statx(dirfd, path, flags, mask, statbuf);
}

DIR* PACKFS_WRAP(opendir)(const char *path)
{
    packfs_init(path);
    if(packfs_enabled && packfs_path_in_range(packfs_archive_prefix, path))
    {
        void* stream = packfs_open(path); //packfs_opendir(path);
        if(stream != NULL)
        {
            int* ptr = packfs_find(-1, stream);
            int fd = ptr == NULL ? -1 : *ptr;
            return (DIR*)stream;
        }
    }
    return __real_opendir(path);
}

DIR* PACKFS_WRAP(fdopendir)(int dirfd)
{
    packfs_init(NULL);
    if(packfs_enabled && packfs_fd_in_range(dirfd))
    {
        DIR* stream = packfs_find(dirfd, NULL);
        if(stream != NULL)
            return stream;
    }
    return __real_fdopendir(dirfd);
}

struct dirent* PACKFS_WRAP(readdir)(DIR* stream)
{
    packfs_init(NULL);
    if(packfs_enabled && packfs_find(-1, stream) != NULL)
    {
        int* ptr = packfs_find(-1, stream);
        if(ptr != NULL)
            return packfs_readdir(stream);
    }
    return __real_readdir(stream);
}

int PACKFS_WRAP(closedir)(DIR* stream)
{
    packfs_init(NULL);
    if(packfs_enabled && packfs_find(-1, stream) != NULL)
    {        
        int* ptr = packfs_find(-1, stream);
        int fd = ptr == NULL ? -1 : *ptr;
        int res = packfs_close(fd);
        if(res >= -1)
            return res;
    }
    return __real_closedir(stream);
}

int PACKFS_WRAP(fcntl)(int fd, int action, ...)
{
    int intarg = -1;
    void* ptrarg = NULL;
    char argtype = ' ';
    va_list arg;
    va_start(arg, action);
    switch(action)
    {
        case F_GETFD:
        case F_GETFL:
        case F_GETLEASE:
        case F_GETOWN:
        case F_GETPIPE_SZ:
        case F_GETSIG:
        case F_GET_SEALS: // linux-specific
        {
            argtype = ' ';
            break;
        }
        case F_ADD_SEALS: // linux-specific
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        case F_NOTIFY:
        case F_SETFD:
        case F_SETFL:
        case F_SETLEASE:
        case F_SETOWN:
        case F_SETPIPE_SZ:
        case F_SETSIG:
        {
            intarg = va_arg(arg, int);
            argtype = '0';
            break;
        }
        default:
        {
            ptrarg = va_arg(arg, void*);
            argtype = '*';
            break;
        }
    }
    va_end(arg);

    packfs_init(NULL);
    
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        int res = (argtype == '0' && (action == F_DUPFD || action == F_DUPFD_CLOEXEC)) ? packfs_dup(fd, intarg) : -1;
        if(res >= -1)
            return res;
    }
    
    return (argtype == '0' ? __real_fcntl(fd, action, intarg) : argtype == '*' ? __real_fcntl(fd, action, ptrarg) : __real_fcntl(fd, action));
}
