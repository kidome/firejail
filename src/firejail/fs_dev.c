/*
 * Copyright (C) 2014-2018 Firejail Authors
 *
 * This file is part of firejail project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include "firejail.h"
#include <sys/mount.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <glob.h>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#include <sys/sysmacros.h>
#include <sys/types.h>

// device type
typedef enum {
	DEV_NONE = 0,
} DEV_TYPE;


typedef struct {
	const char *dev_fname;
	const char *run_fname;
	DEV_TYPE  type;
} DevEntry;

static DevEntry dev[] = {
	{NULL, NULL, DEV_NONE}
};

static void deventry_mount(void) {
	int i = 0;
	while (dev[i].dev_fname != NULL) {
		struct stat s;
		if (stat(dev[i].run_fname, &s) == 0) {
			// check device type and subsystem configuration
          if (0) {
                //                (dev[i].type == DEV_U2F && arg_nou2f == 0)) {

				int dir = is_dir(dev[i].run_fname);
				if (arg_debug)
					printf("mounting %s %s\n", dev[i].run_fname, (dir)? "directory": "file");
				if (dir) {
					mkdir_attr(dev[i].dev_fname, 0755, 0, 0);
				}
				else {
					struct stat s;
					if (stat(dev[i].run_fname, &s) == -1) {
						if (arg_debug)
							fwarning("cannot stat %s file\n", dev[i].run_fname);
						i++;
						continue;
					}
					FILE *fp = fopen(dev[i].dev_fname, "w");
					if (fp) {
						fprintf(fp, "\n");
						SET_PERMS_STREAM(fp, s.st_uid, s.st_gid, s.st_mode);
						fclose(fp);
					}
				}

				if (mount(dev[i].run_fname, dev[i].dev_fname, NULL, MS_BIND|MS_REC, NULL) < 0)
					errExit("mounting dev file");
				fs_logger2("whitelist", dev[i].dev_fname);
			}
		}

		i++;
	}
}

static void create_char_dev(const char *path, mode_t mode, int major, int minor) {
	dev_t dev = makedev(major, minor);
	if (mknod(path, S_IFCHR | mode, dev) == -1)
		goto errexit;
	if (chmod(path, mode) < 0)
		goto errexit;
	ASSERT_PERMS(path, 0, 0, mode);
	fs_logger2("create", path);

	return;

errexit:
	fprintf(stderr, "Error: cannot create %s device\n", path);
	exit(1);
}

static void create_link(const char *oldpath, const char *newpath) {
	if (symlink(oldpath, newpath) == -1) {
		fprintf(stderr, "Error: cannot create %s device\n", newpath);
		exit(1);
	}

	if (chown(newpath, 0, 0) < 0) {;}

	fs_logger2("create", newpath);
	return;
}

static void empty_dev_shm(void) {
	// create an empty /dev/shm directory
	mkdir_attr("/dev/shm", 01777, 0, 0);
	fs_logger("mkdir /dev/shm");
	fs_logger("create /dev/shm");
}

static void mount_dev_shm(void) {
	mkdir_attr("/dev/shm", 01777, 0, 0);
	int rv = mount(RUN_DEV_DIR "/shm", "/dev/shm", "none", MS_BIND, "mode=01777,gid=0");
	if (rv == -1) {
		fwarning("cannot mount the old /dev/shm in private-dev\n");
		dbg_test_dir(RUN_DEV_DIR "/shm");
		empty_dev_shm();
		return;
	}
}

static void process_dev_shm(void) {
	// Jack audio keeps an Unix socket under (/dev/shm/jack_default_1000_0 or /dev/shm/jack/...)
	// looking for jack socket
	glob_t globbuf;
	int globerr = glob(RUN_DEV_DIR "/shm/jack*", GLOB_NOSORT, NULL, &globbuf);
	if (globerr && !arg_keep_dev_shm) {
		empty_dev_shm();
		return;
	}
	globfree(&globbuf);

	// if we got here, it means we have a jack server installed
	// mount-bind the old /dev/shm
	mount_dev_shm();

}


void fs_private_dev(void){
	// install a new /dev directory
	if (arg_debug)
		printf("Mounting tmpfs on /dev\n");

	// create DRI_DIR
	// keep a copy of dev directory
	mkdir_attr(RUN_DEV_DIR, 0755, 0, 0);
	if (mount("/dev", RUN_DEV_DIR, NULL, MS_BIND|MS_REC, NULL) < 0)
		errExit("mounting /dev");

	// create DEVLOG_FILE
	int have_devlog = 0;
	struct stat s;
	if (stat("/dev/log", &s) == 0) {
		have_devlog = 1;
		FILE *fp = fopen(RUN_DEVLOG_FILE, "w");
		if (!fp)
			have_devlog = 0;
		else {
			fprintf(fp, "\n");
			fclose(fp);
			if (mount("/dev/log", RUN_DEVLOG_FILE, NULL, MS_BIND|MS_REC, NULL) < 0)
				errExit("mounting /dev/log");
		}
	}

	// mount tmpfs on top of /dev
	if (mount("tmpfs", "/dev", "tmpfs", MS_NOSUID | MS_STRICTATIME | MS_REC,  "mode=755,gid=0") < 0)
		errExit("mounting /dev");
	fs_logger("tmpfs /dev");

	// optional devices: sound, video cards etc...
	deventry_mount();

	// bring back /dev/log
	if (have_devlog) {
		FILE *fp = fopen("/dev/log", "w");
		if (fp) {
			fprintf(fp, "\n");
			fclose(fp);
			if (mount(RUN_DEVLOG_FILE, "/dev/log", NULL, MS_BIND|MS_REC, NULL) < 0)
				errExit("mounting /dev/log");
			fs_logger("clone /dev/log");
		}
	}

	// bring forward the current /dev/shm directory if necessary
	if (arg_debug)
		printf("Process /dev/shm directory\n");
	process_dev_shm();

	if (mount(RUN_RO_DIR, RUN_DEV_DIR, "none", MS_BIND, "mode=400,gid=0") < 0)
		errExit("disable run dev directory");

	// create default devices
	create_char_dev("/dev/zero", 0666, 1, 5); // mknod -m 666 /dev/zero c 1 5
	fs_logger("mknod /dev/zero");
	create_char_dev("/dev/null", 0666, 1, 3); // mknod -m 666 /dev/null c 1 3
	fs_logger("mknod /dev/null");
	create_char_dev("/dev/full", 0666, 1, 7); // mknod -m 666 /dev/full c 1 7
	fs_logger("mknod /dev/full");
	create_char_dev("/dev/random", 0666, 1, 8); // Mknod -m 666 /dev/random c 1 8
	fs_logger("mknod /dev/random");
	create_char_dev("/dev/urandom", 0666, 1, 9); // mknod -m 666 /dev/urandom c 1 9
	fs_logger("mknod /dev/urandom");
	create_char_dev("/dev/tty", 0666,  5, 0); // mknod -m 666 /dev/tty c 5 0
	fs_logger("mknod /dev/tty");
#if 0
	create_dev("/dev/tty0", "mknod -m 666 /dev/tty0 c 4 0");
	create_dev("/dev/console", "mknod -m 622 /dev/console c 5 1");
#endif

	// pseudo-terminal
	mkdir_attr("/dev/pts", 0755, 0, 0);
	fs_logger("mkdir /dev/pts");
	fs_logger("create /dev/pts");
	create_char_dev("/dev/pts/ptmx", 0666, 5, 2); //"mknod -m 666 /dev/pts/ptmx c 5 2");
	fs_logger("mknod /dev/pts/ptmx");
	create_link("/dev/pts/ptmx", "/dev/ptmx");

// code before github issue #351
	// mount -vt devpts -o newinstance -o ptmxmode=0666 devpts //dev/pts
//	if (mount("devpts", "/dev/pts", "devpts", MS_MGC_VAL,  "newinstance,ptmxmode=0666") < 0)
//		errExit("mounting /dev/pts");


	// mount /dev/pts
	gid_t ttygid = get_group_id("tty");
	char *data;
	if (asprintf(&data, "newinstance,gid=%d,mode=620,ptmxmode=0666", (int) ttygid) == -1)
		errExit("asprintf");
	if (mount("devpts", "/dev/pts", "devpts", MS_MGC_VAL,  data) < 0)
		errExit("mounting /dev/pts");
	free(data);
	fs_logger("clone /dev/pts");

	// stdin, stdout, stderr
#if 0
	create_link("/proc/self/fd", "/dev/fd");
	create_link("/proc/self/fd/0", "/dev/stdin");
	create_link("/proc/self/fd/1", "/dev/stdout");
	create_link("/proc/self/fd/2", "/dev/stderr");
#endif

}


