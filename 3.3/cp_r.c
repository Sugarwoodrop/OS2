
#include "cp_r.h"

int build_path(char* path, size_t size, const char* dir, const char* name) {
    int len_path = snprintf(path, size, "%s/%s", dir, name);
    if (len_path < 0 || (size_t)len_path >= size) {
        return ERROR;
    }
    return SUCCESS;
}

int open_with_retry(const char* path, int flags, mode_t mode) {
    int fd;
    int retries = 0;
    while (retries < MAX_RETRIES) {
        fd = open(path, flags, mode);
        if (fd != ERROR) {
            return fd;  
        }      
        if (errno != EMFILE) {
            printf("open_with_retry: open() failed for %s: %s\n", path, strerror(errno));
            return ERROR;
        }     
        retries++;
        sleep(1);
    }
    return ERROR;
}

DIR* opendir_with_retry(const char* path) {
    DIR* dir;
    int retries = 0;    
    while (retries < MAX_RETRIES) {
        dir = opendir(path);
        if (dir != NULL) {
            return dir; 
        }         
        if (errno != EMFILE) {
            printf("opendir_with_retry: opendir() failed for %s: %s\n", path, strerror(errno));
            return NULL;
        }     
        retries++;
        sleep(1);
    }
    return NULL;
}

int create_directory_safe(const char* src_path, const char* dst_path) {
    int err; 
    struct stat src_stat;
    err = lstat(src_path, &src_stat);
    if (err != SUCCESS) {
        printf("create_directory_safe: lstat() failed for %s: %s\n", src_path, strerror(errno));
        return ERROR;
    }

    err = pthread_mutex_lock(&dir_mutex);
    if (err != SUCCESS) {
        printf("create_directory_safe: pthread_mutex_lock() failed: %s\n", strerror(err));
        return ERROR;
    }

    err = mkdir(dst_path, src_stat.st_mode);
    if (err != SUCCESS && errno != EEXIST) {
        printf("create_directory_safe: mkdir() failed for %s: %s\n", src_path, strerror(errno));
        err = pthread_mutex_unlock(&dir_mutex);
        if (err != SUCCESS) {
            printf("create_directory_safe: pthread_mutex_unlock() failed: %s\n", strerror(err));
        }
        return ERROR;
    } 

    err = pthread_mutex_unlock(&dir_mutex);
    if (err != SUCCESS) {
        printf("create_directory_safe: pthread_mutex_unlock() failed: %s\n", strerror(err));
        return ERROR;
    }    
    return SUCCESS;
}

void *copy_file_thread(void* arg) {
    int err;
    task_t* task = (task_t*)arg;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read, bytes_written;
    int write_error = 0;
    struct stat src_stat;
    err = lstat(task->src_path, &src_stat);
    if (err != SUCCESS) {
        printf("copy_file_thread: lstat() failed for %s: %s\n", task->src_path, strerror(errno));
        free(task);
        return NULL;
    }

    int src_fd = open_with_retry(task->src_path, O_RDONLY, 0);
    if (src_fd == ERROR) {
        printf("copy_file_thread: failed to open source %s\n", task->src_path);
        free(task);
        return NULL;
    }    
    int dst_fd = open_with_retry(task->dst_path, O_WRONLY | O_CREAT | O_TRUNC, src_stat.st_mode);
    if (dst_fd == ERROR) {
        printf("copy_file_thread: failed to create target %s\n", task->dst_path);
        err = close(src_fd);
        if (err != SUCCESS) {
            printf("copy_file_thread: close() failed for source fd: %s\n", strerror(errno));
        }
        free(task);
        return NULL;
    }
    
    while (1) {
        bytes_read = read(src_fd, buffer, BUFFER_SIZE);
        if (bytes_read == ERROR) {
            printf("copy_file_thread: read() error from %s: %s\n", task->src_path, strerror(errno));
            break;
        }        
        if (bytes_read == 0) {
            break;  
        }
        
        ssize_t total_written = 0;
        char* ptr = buffer;
        while (total_written < bytes_read) {
            bytes_written = write(dst_fd, ptr + total_written, bytes_read - total_written);
            if (bytes_written == ERROR) {
                printf("copy_file_thread: write error to %s: %s\n", task->dst_path, strerror(errno));
                write_error = 1;
                break;
            }
            total_written += bytes_written;
        }
        if (write_error) {
            break;  
        }        
    }
    err = close(src_fd);
    if (err != SUCCESS) {
        printf("copy_file_thread: close() failed for source fd: %s\n", strerror(errno));
    }    
    err = close(dst_fd);
    if (err != SUCCESS) {
        printf("copy_file_thread: close() failed for target fd: %s\n", strerror(errno));
    }    
    free(task);
    return NULL;
}

int create_file_task(const char* src_path, const char* dst_path) {
    int err;
    pthread_t thread;
    task_t* task = malloc(sizeof(task_t));
    if (task == NULL) {
        printf("create_file_task: memory allocation failed\n");
        return ERROR;
    }
    strcpy(task->src_path, src_path);
    strcpy(task->dst_path, dst_path);    
    
    err = pthread_create(&thread, NULL, copy_file_thread, task);
	if (err != SUCCESS) {
		printf("create_file_task: pthread_create() failed: %s\n", strerror(err));
        free(task);
		return ERROR;
	}
    err = pthread_detach(thread);
    if (err != SUCCESS) {
        printf("create_file_task: pthread_detach() failed: %s\n", strerror(err));
    }    
    return SUCCESS;
}

int create_directory_task(const char* src_path, const char* dst_path) {
    int err;
    pthread_t thread;
    task_t* task = malloc(sizeof(task_t));
    if (task == NULL) {
        printf("create_directory_task: memory allocation failed\n");
        return ERROR;
    }  
    strcpy(task->src_path, src_path);
    strcpy(task->dst_path, dst_path);        
    
    err = pthread_create(&thread, NULL, work_directory_thread, task);
	if (err != SUCCESS) {
		printf("create_directory_task: pthread_create() failed: %s\n", strerror(err));
        free(task);
		return ERROR;
	}
    err = pthread_detach(thread);
    if (err != SUCCESS) {
        printf("create_directory_task: pthread_detach() failed: %s\n", strerror(err));
    }    
    return SUCCESS;
}

int process_single_entry(const char* src_dir, const char* dst_dir, const char* entry_name) {
    int err;
    char src_path[PATH_MAX];
    char dst_path[PATH_MAX];
    struct stat stat_buf;
    err = build_path(src_path, sizeof(src_path), src_dir, entry_name);
    if (err != SUCCESS) {
        printf("process_single_entry: source path too long: %s/%s\n", src_dir, entry_name);
        return ERROR;
    }    
    err = build_path(dst_path, sizeof(dst_path), dst_dir, entry_name);
    if (err != SUCCESS) {
        printf("process_single_entry: destination path too long: %s/%s\n", dst_dir, entry_name);
        return ERROR;
    }

    err = lstat(src_path, &stat_buf);
    if (err != SUCCESS) {
        printf("process_single_entry: lstat() failed for %s: %s\n", src_path, strerror(errno));
        return ERROR;
    }
    if (S_ISDIR(stat_buf.st_mode)) {
        return create_directory_task(src_path, dst_path);
    }
    if (S_ISREG(stat_buf.st_mode)) {
        return create_file_task(src_path, dst_path);
    }    
    return SUCCESS;
}

void *work_directory_thread(void* arg) {
    int err;
    task_t* task = (task_t*)arg;
    err = create_directory_safe(task->src_path, task->dst_path);
    if (err != SUCCESS) {
        printf("work_directory_thread: failed to create directory %s\n", task->dst_path);
        free(task);
        return NULL;
    }    

    DIR* dir = opendir_with_retry(task->src_path);
    if (dir == NULL) {
        printf("work_directory_thread: failed to open directory %s\n", task->src_path);
        free(task);
        return NULL;
    }

    struct dirent* entry;
    while (1) {
        errno = SUCCESS;
        entry = readdir(dir); 
        if (entry == NULL && errno != SUCCESS) {
            printf("work_directory_thread: readdir error: %s\n", strerror(errno));
            err = ERROR;
            break;
        }   
        if (entry == NULL) {
            err = SUCCESS;
            break; 
        }                
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        err = process_single_entry(task->src_path, task->dst_path, entry->d_name);
        if (err != SUCCESS) {
            printf("work_directory_thread: failed to add task for %s\n", entry->d_name);
        }
    } 
    err = closedir(dir);
    if (err != SUCCESS) {
        printf("work_directory_thread: closedir() failed: %s\n", strerror(errno));
    }    
    free(task);
    return NULL;
}

int main(int argc, char* argv[]) {   
    int err;
    struct stat stat_buf; 
    pthread_t main_thread;
    if (argc != 3) {
        printf("Use %s source_directory target_directory\n", argv[0]);
        return ERROR;
    }        
    err = lstat(argv[1], &stat_buf);
    if (err != SUCCESS) {
        printf("main: lstat() failed: %s\n", strerror(errno));
        return ERROR;
    }
    if (S_ISDIR(stat_buf.st_mode) != true) {
        printf("main: Source path %s is not a directory\n", argv[1]);
        return ERROR;
    }

    err = pthread_mutex_init(&dir_mutex, NULL);
	if (err != SUCCESS) {
		printf("main: pthread_mutex_init() failed: %s\n", strerror(err));
		return ERROR;
	}

    task_t* task = malloc(sizeof(task_t));
    if (task == NULL) {
        printf("main: memory allocation failed\n");
        err = pthread_mutex_destroy(&dir_mutex);
        if (err != SUCCESS) {
            printf("main: pthread_mutex_destroy() failed: %s\n", strerror(err));
        }
        return ERROR;
    }
    strcpy(task->src_path, argv[1]);
    strcpy(task->dst_path, argv[2]);
    
    err = pthread_create(&main_thread, NULL, work_directory_thread, task);
	if (err != SUCCESS) {
		printf("main: pthread_create() failed: %s\n", strerror(err));
        free(task);
		err = pthread_mutex_destroy(&dir_mutex);
		if (err != SUCCESS) {
            printf("main: pthread_mutex_destroy() failed: %s\n", strerror(err));
        }
		return ERROR;
	}
    err = pthread_join(main_thread, NULL);
	if (err != SUCCESS) {
		printf("main: pthread_join() failed: %s\n", strerror(err));
        err = pthread_mutex_destroy(&dir_mutex);
		if (err != SUCCESS) {
            printf("main: pthread_mutex_destroy() failed: %s\n", strerror(err));
        }
		return ERROR;
	}
    sleep(2);
    err = pthread_mutex_destroy(&dir_mutex);
	if (err != SUCCESS) {
		printf("main: pthread_mutex_destroy() failed: %s\n", strerror(err));
	}
    printf("main: all tasks were completed\n");
    return SUCCESS;
}
