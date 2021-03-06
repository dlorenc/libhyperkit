// +build qcow2

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include <caml/alloc.h>
#include <caml/threads.h>
#include <caml/mlvalues.h>
#include <caml/memory.h>
#include <caml/callback.h>
#include <caml/bigarray.h>

#include "mirage_block_c.h"

void
mirage_block_register_thread(){
	caml_c_thread_register();
}

void
mirage_block_unregister_thread(){
	caml_c_thread_unregister();
}

/* Convenience macro to cache the OCaml callback and immediately abort()
   if it can't be found -- this would indicate a fundamental linking error. */
#define OCAML_NAMED_FUNCTION(name) \
static value *fn = NULL; \
if (fn == NULL) { \
	fn = caml_named_value(name); \
} \
if (fn == NULL) { \
	fprintf(stderr, "Callback.register for " name " not called: are all objects linked?\n"); \
	abort(); \
}

/* Every call has 2 C functions:
		1. static void ocaml_FOO: using the CAMLparam/CAMLreturn macros. This assumes
			the runtime system lock is held. Errors are propagated back by out
			parameters, since we must use CAMLreturn and yet we cannot use CAMLreturn
			for regular C values.
		2. plain C FOO: this acquires the runtime lock and calls the ocaml_FOO
			function.
	An alternative design would be to use Begin_roots and End_roots after
	acquiring the runtime lock. */

static void
ocaml_mirage_block_open(const char *uri, int buffered, int *out, int *err) {
	CAMLparam0();
	CAMLlocal3(ocaml_uri, ocaml_buffered, handle);
	ocaml_uri = caml_copy_string(uri);
	ocaml_buffered = Val_bool(buffered);
	OCAML_NAMED_FUNCTION("mirage_block_open")
	handle = caml_callback2_exn(*fn, ocaml_uri, ocaml_buffered);
	if (Is_exception_result(handle)){
		*err = 1;
	} else {
		*err = 0;
		*out = Int_val(handle);
	}
	CAMLreturn0;
}

mirage_block_handle
mirage_block_open(const char *uri, int buffered) {
	int result;
	int err = 1;
	caml_acquire_runtime_system();
	ocaml_mirage_block_open(uri, buffered, &result, &err);
	caml_release_runtime_system();
	if (err){
		errno = EINVAL;
		return (-1);
	} else {
		return result;
	}
}

static void
ocaml_mirage_block_stat(mirage_block_handle h, struct stat *out, int *err) {
	CAMLparam0();
	CAMLlocal2(ocaml_handle, result);
	ocaml_handle = Val_int(h);
	OCAML_NAMED_FUNCTION("mirage_block_stat")
	result = caml_callback_exn(*fn, ocaml_handle);
	int read_write = Int_val(Field(result, 0)) != 0;
	unsigned int sector_size = (unsigned int)Int_val(Field(result, 1));
	uint64_t size_sectors = (uint64_t)Int64_val(Field(result, 2));
	if (Is_exception_result(result)){
		*err = 1;
	} else {
		*err = 0;
		bzero(out, sizeof(struct stat));
		out->st_dev = 0;
		out->st_ino = 0;
		out->st_mode = S_IFREG | S_IROTH | S_IRGRP | S_IRUSR | (read_write?(S_IWOTH | S_IWGRP | S_IWUSR): 0);
		out->st_nlink = 1;
		out->st_uid = 0;
		out->st_gid = 0;
		out->st_rdev = 0;
		out->st_size = (off_t)(sector_size * size_sectors);
		out->st_blocks = (blkcnt_t)size_sectors;
		out->st_blksize = (blksize_t)sector_size;
		out->st_flags = 0;
		out->st_gen = 0;
	}
	CAMLreturn0;
}

int
mirage_block_stat(mirage_block_handle h, struct stat *buf) {
	int err = 1;
	caml_acquire_runtime_system();
	ocaml_mirage_block_stat(h, buf, &err);
	caml_release_runtime_system();
	if (err){
		errno = EINVAL;
		return (-1);
	} else {
		return 0;
	}
}

static void
ocaml_mirage_block_close(int handle, int *err) {
	CAMLparam0();
	CAMLlocal2(ocaml_handle, result);
	ocaml_handle = Val_int(handle);
	OCAML_NAMED_FUNCTION("mirage_block_close")
	result = caml_callback_exn(*fn, ocaml_handle);
	*err = 0;
	if (Is_exception_result(result)){
		*err = 1;
	}
	CAMLreturn0;
}

int mirage_block_close(int handle){
	int err = 1;
	caml_acquire_runtime_system();
	ocaml_mirage_block_close(handle, &err);
	caml_release_runtime_system();
	return err;
}

static void
ocaml_mirage_block_preadv(const int handle, const struct iovec *iov, int iovcnt, off_t ofs, ssize_t *out, int *err) {
	CAMLparam0();
	CAMLlocal4(ocaml_handle, ocaml_bufs, ocaml_ofs, ocaml_result);
	ocaml_handle = Val_int(handle);
	ocaml_bufs = caml_alloc_tuple((mlsize_t)iovcnt);
	ocaml_ofs = Val_int(ofs);
	for (int i = 0; i < iovcnt; i++ ){
		Store_field(ocaml_bufs, (mlsize_t)i, caml_ba_alloc_dims(CAML_BA_CHAR | CAML_BA_C_LAYOUT,
			1, (*(iov+i)).iov_base, (*(iov+i)).iov_len));
	}
	OCAML_NAMED_FUNCTION("mirage_block_preadv")
	ocaml_result = caml_callback3_exn(*fn, ocaml_handle, ocaml_bufs, ocaml_ofs);
	if (Is_exception_result(ocaml_result)) {
		*err = 1;
	} else {
		*err = 0;
		*out = Int_val(ocaml_result);
	}
	CAMLreturn0;
}

ssize_t
mirage_block_preadv(mirage_block_handle h, const struct iovec *iov, int iovcnt, off_t offset) {
	ssize_t len;
	int err = 1;
	caml_acquire_runtime_system();
	ocaml_mirage_block_preadv(h, iov, iovcnt, offset, &len, &err);
	caml_release_runtime_system();
	if (err){
		errno = EINVAL;
		return (-1);
	}
	return len;
}
static void
ocaml_mirage_block_pwritev(const int handle, const struct iovec *iov, int iovcnt, off_t ofs, ssize_t *out, int *err) {
	CAMLparam0();
	CAMLlocal4(ocaml_handle, ocaml_bufs, ocaml_ofs, ocaml_result);
	ocaml_handle = Val_int(handle);
	ocaml_bufs = caml_alloc_tuple((mlsize_t)iovcnt);
	ocaml_ofs = Val_int(ofs);
	for (int i = 0; i < iovcnt; i++ ){
		Store_field(ocaml_bufs, (mlsize_t)i, caml_ba_alloc_dims(CAML_BA_CHAR | CAML_BA_C_LAYOUT,
			1, (*(iov+i)).iov_base, (*(iov+i)).iov_len));
	}
	OCAML_NAMED_FUNCTION("mirage_block_pwritev")
	ocaml_result = caml_callback3_exn(*fn, ocaml_handle, ocaml_bufs, ocaml_ofs);
	if (Is_exception_result(ocaml_result)) {
		*err = 1;
	} else {
		*err = 0;
		*out = Int_val(ocaml_result);
	}
	CAMLreturn0;
}

ssize_t
mirage_block_pwritev(mirage_block_handle h, const struct iovec *iov, int iovcnt, off_t offset) {
	ssize_t len;
	int err = 1;
	caml_acquire_runtime_system();
	ocaml_mirage_block_pwritev(h, iov, iovcnt, offset, &len, &err);
	caml_release_runtime_system();
	if (err){
		errno = EINVAL;
		return (-1);
	}
	return len;
}

static void
ocaml_mirage_block_flush(int handle, int *err) {
	CAMLparam0();
	CAMLlocal2(ocaml_handle, result);
	ocaml_handle = Val_int(handle);
	OCAML_NAMED_FUNCTION("mirage_block_flush")
	result = caml_callback_exn(*fn, ocaml_handle);
	*err = 0;
	if (Is_exception_result(result)){
		errno = EINVAL;
		*err = 1;
	}
	CAMLreturn0;
}

int mirage_block_flush(int handle){
	int err = 1;
	caml_acquire_runtime_system();
	ocaml_mirage_block_flush(handle, &err);
	caml_release_runtime_system();
	return err;
}
