#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fs.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define test_fs_error(fmt, ...) \
	fprintf(stderr, "%s: "fmt"\n", __func__, ##__VA_ARGS__)

#define die(...)				\
do {							\
	test_fs_error(__VA_ARGS__);	\
	exit(1);					\
} while (0)

#define die_perror(msg)			\
do {							\
	perror(msg);				\
	exit(1);					\
} while (0)

struct thread_arg {
	int argc;
	char **argv;
};

void thread_fs_stat(void *arg)
{
	struct thread_arg *t_arg = arg;
	char *diskname, *filename;
	int fs_fd;
	int stat;

	if (t_arg->argc < 2)
		die("need <diskname> <filename>");

	diskname = t_arg->argv[0];
	filename = t_arg->argv[1];

	if (fs_mount(diskname))
		die("Cannot mount diskname");

	fs_fd = fs_open(filename);
	if (fs_fd < 0) {
		fs_umount();
		die("Cannot open file");
	}

	stat = fs_stat(fs_fd);
	if (stat < 0) {
		fs_umount();
		die("Cannot stat file");
	}
	if (!stat) {
		if(fs_close(fs_fd))
		{
			fs_umount();
			die("Cannot close file");
		}
		printf("Size of file '%s' is %d bytes\n", filename, stat);
		//add by yuan, not good to fix client's error in library

		fs_umount();
		/* Nothing to read, file is empty */
		printf("Empty file\n");
		return;
	}

	if (fs_close(fs_fd)) {
		fs_umount();
		die("Cannot close file");
	}

	if (fs_umount())
		die("cannot unmount diskname");

	printf("Size of file '%s' is %d bytes\n", filename, stat);
}

void thread_fs_cat(void *arg)
{
	struct thread_arg *t_arg = arg;
	char *diskname, *filename, *buf;
	int fs_fd;
	int stat, read;

	if (t_arg->argc < 2)
		die("need <diskname> <filename>");

	diskname = t_arg->argv[0];
	filename = t_arg->argv[1];

	if (fs_mount(diskname))
		die("Cannot mount diskname");

	fs_fd = fs_open(filename);
	if (fs_fd < 0) {
		fs_umount();
		die("Cannot open file");
	}

	stat = fs_stat(fs_fd);
	if (stat < 0) {
		fs_umount();
		die("Cannot stat file");
	}
	if (!stat) {
		/* Nothing to read, file is empty */
		printf("Empty file\n");
		return;
	}
	buf = malloc(stat);
	if (!buf) {
		perror("malloc");
		fs_umount();
		die("Cannot malloc");
	}

	read = fs_read(fs_fd, buf, stat);

	if (fs_close(fs_fd)) {
		fs_umount();
		die("Cannot close file");
	}

	if (fs_umount())
		die("cannot unmount diskname");

	printf("Read file '%s' (%d/%d bytes)\n", filename, read, stat);
	printf("Content of the file:\n");
	printf("%.*s", (int)stat, buf);

	free(buf);
}

void thread_fs_rm(void *arg)
{
	struct thread_arg *t_arg = arg;
	char *diskname, *filename;

	if (t_arg->argc < 2)
		die("need <diskname> <filename>");

	diskname = t_arg->argv[0];
	filename = t_arg->argv[1];

	if (fs_mount(diskname))
		die("Cannot mount diskname");

	if (fs_delete(filename)) {
		fs_umount();
		die("Cannot delete file");
	}

	if (fs_umount())
		die("Cannot unmount diskname");

	printf("Removed file '%s'\n", filename);
}

void thread_fs_add(void *arg)
{
	struct thread_arg *t_arg = arg;
	char *diskname, *filename, *buf;
	int fd, fs_fd;
	struct stat st;
	int written;

	if (t_arg->argc < 2)
		die("Usage: <diskname> <host filename>");

	diskname = t_arg->argv[0];
	filename = t_arg->argv[1];

	/* Open file on host computer */
	fd = open(filename, O_RDONLY);
	if (fd < 0)
		die_perror("open");
	if (fstat(fd, &st))
		die_perror("fstat");
	if (!S_ISREG(st.st_mode))
		die("Not a regular file: %s\n", filename);

	/* Map file into buffer */
	buf = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (!buf)
		die_perror("mmap");

	/* Now, deal with our filesystem:
	 * - mount, create a new file, copy content of host file into this new
	 *   file, close the new file, and umount
	 */
	if (fs_mount(diskname))
		die("Cannot mount diskname");

	if (fs_create(filename)) {
		fs_umount();
		die("Cannot create file");
	}

	fs_fd = fs_open(filename);
	if (fs_fd < 0) {
		fs_umount();
		die("Cannot open file");
	}

	written = fs_write(fs_fd, buf, st.st_size);

	if (fs_close(fs_fd)) {
		fs_umount();
		die("Cannot close file");
	}

	if (fs_umount())
		die("Cannot unmount diskname");

	printf("Wrote file '%s' (%d/%zu bytes)\n", filename, written,
		   st.st_size);

	munmap(buf, st.st_size);
	close(fd);
}

void thread_fs_ls(void *arg)
{
	struct thread_arg *t_arg = arg;
	char *diskname;

	if (t_arg->argc < 1)
		die("Usage: <diskname> <filename>");

	diskname = t_arg->argv[0];

	if (fs_mount(diskname))
		die("Cannot mount diskname");

	fs_ls();

	if (fs_umount())
		die("Cannot unmount diskname");
}

void thread_fs_info(void *arg)
{
	struct thread_arg *t_arg = arg;
	char *diskname;

	if (t_arg->argc < 1)
		die("Usage: <diskname>");

	diskname = t_arg->argv[0];

	if (fs_mount(diskname))
		die("Cannot mount diskname");

	fs_info();

	if (fs_umount())
		die("Cannot unmount diskname");
}

size_t get_argv(char *argv)
{
	long int ret = strtol(argv, NULL, 0);
	if (ret == LONG_MIN || ret == LONG_MAX)
		die_perror("strtol");
	return (size_t)ret;
}

//yuan
void thread_fs_read(void *arg)
{
	struct thread_arg *t_arg = arg;
	char *diskname, *filename, *buf;
	int fs_fd;
	int stat, read;
	int offset, count = 0;

	if (t_arg->argc < 3)
		die("need <diskname> <filename> <offset>");

	diskname = t_arg->argv[0];
	filename = t_arg->argv[1];
	offset = atoi(t_arg->argv[2]);
	if(t_arg->argc >=4)
		count = atoi(t_arg->argv[3]);

	if (fs_mount(diskname))
		die("Cannot mount diskname");

	fs_fd = fs_open(filename);
	if (fs_fd < 0) {
		fs_umount();
		die("Cannot open file");
	}

	stat = fs_stat(fs_fd);
	if (stat < 0) {
		fs_umount();
		die("Cannot stat file");
	}
	if (!stat) {
		/* Nothing to read, file is empty */
		fs_close(fs_fd);
		fs_umount();
		// printf("Empty file\n");
		die("Empty file");
	}

	// printf("offset = %d, stat = %d\n", offset, stat);
	if(fs_lseek(fs_fd, offset) < 0){
		fs_close(fs_fd);
		fs_umount();
		die("Offset out of boundary");
	}

	if(count > stat - offset)
		count = stat - offset;

	buf = malloc(count);
	if (!buf) {
		perror("malloc");
		fs_close(fs_fd);
		fs_umount();
		die("Cannot malloc");
	}

	

	read = fs_read(fs_fd, buf, count);

	if (fs_close(fs_fd)) {
		fs_umount();
		die("Cannot close file");
	}

	if (fs_umount())
		die("cannot unmount diskname");

	printf("Read file '%s' (%d/%d bytes) with offset '%d'\n", filename, read, stat, offset);
	printf("Content of the file:\n");
	printf("%.*s\n", count, buf);

	free(buf);
}

void thread_fs_read_multiple(void *arg)
{
	struct thread_arg *t_arg = arg;
	char *diskname, *filename, *buf = NULL;

	int stat, read;
	int itr;

	if (t_arg->argc < 3)
		die("need <diskname> <filename> <times>");

	diskname = t_arg->argv[0];
	filename = t_arg->argv[1];
	itr = atoi(t_arg->argv[2]);
	int * fs_fd = malloc(itr * sizeof(int));

	if (fs_mount(diskname))
		die("Cannot mount diskname");

	int offset;
	for (int i = 0; i < itr; ++i)
		fs_fd[i] = -1;

	for (int i = 0; i < itr; ++i)
	{
		fs_fd[i] = fs_open(filename);
		if (fs_fd[i] < 0) {
			// fs_umount();
			test_fs_error("Cannot open file at %d\n", i);
			break;
		}
		stat = fs_stat(fs_fd[i]);
		if (stat < 0) {
			test_fs_error("Cannot stat file at %d\n", i);
			break;
		}
		if (!stat) {
			/* Nothing to read, file is empty */
			printf("Empty file\n");
			break;
		}
		offset = i;
		buf = malloc(stat-offset);
		if (!buf) {
			perror("malloc");
			test_fs_error("Cannot malloc");
			break;
		}

		if(fs_lseek(fs_fd[i], offset) < 0){
			test_fs_error("Offset out of boundary");
			break;
		}

		read = fs_read(fs_fd[i], buf, stat-offset);
		printf("Read file '%s' (%d/%d bytes) with offset '%d'\n", filename, read, stat, offset);
		printf("Content of the file:\n");
		printf("%.*s\n", (int)(stat-offset), buf);
		free(buf);
		buf = NULL; // or will double free at line 419
	}
	for (int i = 0; i < itr; ++i)
	{
		if(fs_fd[i] >= 0){
			if(fs_close(fs_fd[i]))
				test_fs_error("Cannot close file");
		}
	}

	if (fs_umount())
		die("cannot unmount diskname");

	if(buf) free(buf);
}

//opt 0: buffer, 1: file, 2: char*
void thread_fs_write(void *arg)
{
	struct thread_arg *t_arg = arg;
	char *diskname, *filename, *buf;
	int fs_fd;
	int stat, write;
	int offset, count;
	char* opt = NULL;
	struct stat st;

	if (t_arg->argc < 5)
		die("need <diskname> <filename> <buffer> <offset> <count> <opt>\nyou give %d arguments", t_arg->argc);

	diskname = t_arg->argv[0];
	filename = t_arg->argv[1];
	buf = t_arg->argv[2];
	offset = atoi(t_arg->argv[3]);
	count = atoi(t_arg->argv[4]);
	if(t_arg->argc > 5)
		opt = t_arg->argv[5];


	if (fs_mount(diskname))
		die("Cannot mount diskname");

	fs_fd = fs_open(filename);
	if (fs_fd < 0) {
		fs_umount();
		die("Cannot open file");
	}

	stat = fs_stat(fs_fd);
	if (stat < 0) {
		fs_close(fs_fd);
		fs_umount();
		die("Cannot stat file");
	}

	if(offset > stat){
		fs_close(fs_fd);
		fs_umount();
		die("Offset could larger than file size\n");
	}

	if(fs_lseek(fs_fd, offset) < 0){
		fs_close(fs_fd);
		fs_umount();
		die("Offset out of boundary");
	}

	if(opt != NULL && strcmp(opt, "file") == 0){
		//write form one file to another file
		int fd = open(buf, O_RDONLY);
		if (fd < 0)
			die_perror("open");
		if (fstat(fd, &st))
			die_perror("fstat");
		if (!S_ISREG(st.st_mode))
			die("Not a regular file: %s\n", filename);

		/* Map file into buffer */
		buf = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if(count > st.st_size)
			count = st.st_size;
		write = fs_write(fs_fd, buf, count);

		munmap(buf, st.st_size);
		close(fd);
	}
	else{ // char*, buffer
		write = fs_write(fs_fd, buf, count);
	}
	

	if (fs_close(fs_fd)) {
		fs_umount();
		die("Cannot close file");
	}

	if (fs_umount())
		die("cannot unmount diskname");

	printf("Write file '%s' (%d/%d bytes) with offset '%d'\n", filename, write, stat, offset);
	printf("Content of the file:\n");
	// printf("%.*s\n", (int)(stat-offset), buf);
	// struct thread_arg arg = {
	// 	.argc = 3;
	// 	.argv = malloc(3*sizeof(char*));
	// };
	struct thread_arg arg2;
	arg2.argc = 4;
	arg2.argv = malloc(4*sizeof(char*));
	arg2.argv[0] = diskname;
	arg2.argv[1] = filename;
	arg2.argv[2] = "0";
	arg2.argv[3] = "100";

	thread_fs_read(&arg2);

	free(arg2.argv);

}



static struct {
	const char *name;
	void(*func)(void *);
} commands[] = {
	{ "info",	thread_fs_info },
	{ "ls",		thread_fs_ls },
	{ "add",	thread_fs_add },
	{ "rm",		thread_fs_rm },
	{ "cat",	thread_fs_cat },
	{ "stat",	thread_fs_stat },
	{ "read",	thread_fs_read },
	{ "readm",	thread_fs_read_multiple }, // open multiple files and read
	{ "write",	thread_fs_write },
};

void usage(char *program)
{
	int i;
	fprintf(stderr, "Usage: %s <command> [<arg>]\n", program);
	fprintf(stderr, "Possible commands are:\n");
	for (i = 0; i < ARRAY_SIZE(commands); i++)
		fprintf(stderr, "\t%s\n", commands[i].name);
	exit(1);
}

int main(int argc, char **argv)
{
	int i;
	char *program;
	char *cmd;
	struct thread_arg arg;

	program = argv[0];

	if (argc == 1)
		usage(program);

	/* Skip argv[0] */
	argc--;
	argv++;

	cmd = argv[0];
	arg.argc = --argc;
	arg.argv = &argv[1];

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (!strcmp(cmd, commands[i].name)) {
			commands[i].func(&arg);
			break;
		}
	}

	if (i == ARRAY_SIZE(commands)) {
		test_fs_error("invalid command '%s'", cmd);
		usage(program);
	}

	return 0;
}
