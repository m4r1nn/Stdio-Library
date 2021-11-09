#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#define BUFF_SIZE 4096

#include "so_stdio.h"

// main struct
struct _so_file {
	int fd;
	char buffer[BUFF_SIZE];
	int size;
	int current;
	int last_op;
	long file_pos;
	int eof;
	int error;
	int pid;
};

FUNC_DECL_PREFIX SO_FILE *so_fopen(const char *pathname, const char *mode)
{
	struct _so_file *stream = malloc(sizeof(struct _so_file));

	memset(stream->buffer, 0, BUFF_SIZE);
	stream->size = 0;
	stream->current = 0;
	stream->last_op = 0;
	stream->file_pos = 0;
	stream->eof = 0;
	stream->error = 0;
	stream->pid = 0;

	// check permissions
	if (strcmp(mode, "r") == 0)
		stream->fd = open(pathname, O_RDONLY);
	else if (strcmp(mode, "r+") == 0)
		stream->fd = open(pathname, O_RDWR);
	else if (strcmp(mode, "w") == 0)
		stream->fd = open(pathname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	else if (strcmp(mode, "w+") == 0)
		stream->fd = open(pathname, O_RDWR | O_CREAT | O_TRUNC, 0644);
	else if (strcmp(mode, "a") == 0)
		stream->fd = open(pathname, O_APPEND | O_WRONLY | O_CREAT, 0644);
	else if (strcmp(mode, "a+") == 0)
		stream->fd = open(pathname, O_RDWR | O_CREAT | O_APPEND, 0644);
	else
		stream->fd = SO_EOF;

	// handle open error
	if (stream->fd == SO_EOF) {
		free(stream);
		stream = NULL;
		return NULL;
	}

	return stream;
}

FUNC_DECL_PREFIX int so_fclose(SO_FILE *stream)
{
	// reset buffer
	int res_fflush = so_fflush(stream);
	int res_close = close(stream->fd);

	if (stream != NULL) {
		free(stream);
		stream = NULL;
	}

	if (res_close != 0 || res_fflush != 0)
		return SO_EOF;

	return 0;
}

FUNC_DECL_PREFIX int so_fflush(SO_FILE *stream)
{
	int fflush_error = 0;
	int res;
	int to_print;
	int offset;

	// check if there are bytes left to write
	if (stream->last_op == 1 && stream->current > 0) {
		to_print = stream->current;
		offset = 0;

		while (1) {

			// write back until buffer becomes empty
			res = write(stream->fd, stream->buffer + offset, to_print);
			if (res < 0 || res == to_print) {
				if (res < 0) {
					stream->error = 1;
					fflush_error = 1;
				}
				break;
			}

			to_print -= res;
			offset += res;
		}

		// reset buffer
		memset(stream->buffer, 0, BUFF_SIZE);
		stream->current = 0;
		stream->size = 0;
		stream->last_op = 0;
	}

	if (fflush_error == 1)
		return SO_EOF;

	return 0;
}

FUNC_DECL_PREFIX int so_fseek(SO_FILE *stream, long offset, int whence)
{
	so_fflush(stream);
	stream->file_pos = lseek(stream->fd, offset, whence);

	// reset buffer
	if (stream->last_op == 0) {
		memset(stream->buffer, 0, BUFF_SIZE);
		stream->size = 0;
		stream->current = 0;
	}

	return 0;
}

FUNC_DECL_PREFIX long so_ftell(SO_FILE *stream)
{
	return stream->file_pos;
}

FUNC_DECL_PREFIX
size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int i;
	unsigned char chr;

	// read bytes 1 by 1
	for (i = 0; i < size * nmemb; ++i) {
		chr = so_fgetc(stream);
		if (chr == SO_EOF || stream->eof == 1 || stream->error != 0)
			break;
		memcpy(ptr + i, &chr, 1);
	}

	stream->file_pos += size * nmemb;

	return i / size;
}

FUNC_DECL_PREFIX
size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int i;
	char chr;

	// write bytes 1 by 1
	for (i = 0; i < size * nmemb; ++i) {
		memcpy(&chr, ptr + i, 1);
		so_fputc(chr, stream);
	}

	stream->file_pos += size * nmemb;

	return i / size;
}

FUNC_DECL_PREFIX int so_fileno(SO_FILE *stream)
{
	return stream->fd;
}

FUNC_DECL_PREFIX int so_fgetc(SO_FILE *stream)
{
	stream->last_op = 0;

	// check if there are already bytes in buffer
	if (stream->size == 0 || stream->size == stream->current) {

		memset(stream->buffer, 0, BUFF_SIZE);
		stream->size = read(stream->fd, stream->buffer, BUFF_SIZE);

		// handle errors and flags
		if (stream->size <= 0) {
			if (stream->size == 0)
				stream->eof = 1;
			else
				stream->error = 1;
			return SO_EOF;
		}

		// reset buffer pointer
		stream->current = 0;
	}

	return stream->buffer[stream->current++];
}

FUNC_DECL_PREFIX int so_fputc(int c, SO_FILE *stream)
{
	stream->last_op = 1;
	memcpy(stream->buffer + stream->current, &c, 1);
	stream->current++;

	// check if buffer is full
	if (stream->current == BUFF_SIZE)
		so_fflush(stream);

	return c;
}

FUNC_DECL_PREFIX int so_feof(SO_FILE *stream)
{
	return stream->eof;
}

FUNC_DECL_PREFIX int so_ferror(SO_FILE *stream)
{
	return stream->error;
}

FUNC_DECL_PREFIX SO_FILE *so_popen(const char *command, const char *type)
{
	struct _so_file *stream = malloc(sizeof(struct _so_file));
	int filedes[2];
	int pid;

	// open pipe and fork
	pipe(filedes);
	pid = fork();
	stream->pid = 0;

	// error
	if (pid == -1) {
		free(stream);
		return NULL;
	}

	// child
	if (pid == 0) {

		if (strcmp(type, "r") == 0) {
			close(filedes[0]);
			if (filedes[1] != STDOUT_FILENO)
				dup2(filedes[1], STDOUT_FILENO);

		} else if (strcmp(type, "w") == 0) {
			close(filedes[1]);
			if (filedes[0] != STDIN_FILENO)
				dup2(filedes[0], STDIN_FILENO);
		}

		execlp("sh", "sh", "-c", command, NULL);
		return NULL;
	}

	// parent
	if (strcmp(type, "r") == 0) {
		close(filedes[1]);
		stream->fd = filedes[0];

	} else if (strcmp(type, "w") == 0) {
		close(filedes[0]);
		stream->fd = filedes[1];
	}

	memset(stream->buffer, 0, BUFF_SIZE);
	stream->size = 0;
	stream->current = 0;
	stream->last_op = 0;
	stream->file_pos = 0;
	stream->eof = 0;
	stream->error = 0;
	stream->pid = pid;

	return stream;
}

FUNC_DECL_PREFIX int so_pclose(SO_FILE *stream)
{
	int status;
	int wait = stream->pid;

	// wait for the process to close
	int res_close = so_fclose(stream);
	int pid = waitpid(wait, &status, 0);

	if (pid == SO_EOF || res_close == SO_EOF || status != 0)
		return SO_EOF;

	return 0;
}
