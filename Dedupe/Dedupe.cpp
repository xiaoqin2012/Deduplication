// Dedupe.cpp : Defines the entry point for the console application.
//
#include "stdafx.h"
#include "dedupe.h"

#include <io.h>
#include <time.h>
#include <direct.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define _CRT_SECURE_NO_WARNINGS
#define HashTableSize 1000
#define BUF_SIZE (2<<10)
#define MAX_PATH 512

typedef struct _finddata_t FileAttr;

typedef struct fileAttrP {
	char path[MAX_PATH];
	__int64 hash;
	__int64 size;
	struct fileAttrP *next;
} FileAttrP;

unsigned __int64 savedSpace = 0;
unsigned __int64 totalSpace = 0;
unsigned __int64 num_dup = 0;
unsigned __int64 num_files = 0;
bool delete_option;

int getHashIndex(__int64 size) {
	return size % HashTableSize;
}

FileAttrP* create_fileAttr(FileAttr *c_file, char *dir) {
	FileAttrP *p = (FileAttrP *)malloc(sizeof(FileAttrP));
	p->next = NULL;
	sprintf(p->path, "%s\\%s", dir, c_file->name);
	p->hash = 0;
	p->size = c_file->size;
	return p;
}

void free_fileAttrP(FileAttrP *p) {
	free(p);
}

void insert_ht(FileAttrP **ht, FileAttr *c_file, char *dir) {	
	FileAttrP *p = create_fileAttr(c_file, dir);
	int index = getHashIndex(p->size);
	p->next = ht[index];
	ht[index] = p;
	totalSpace += c_file->size;
	return;
}

void init_ht(FileAttrP **ht, int size) {
	for (int i = 0; i < size; i++)
		ht[i] = NULL;
}

void clean_ht(FileAttrP **ht, int size) {
	FileAttrP *next;
	FileAttrP *current;

	for (int i = 0; i < size; i++) {
		current = ht[i];
		while (current!= NULL) {
			next = current->next;
			free_fileAttrP(current);
			current = next;
		}
		ht[i] = NULL;
	}
}

__int64 file_hash(char *buf) {
	return (__int64)*buf;
}


/* read data to 1K buffer and compare */
bool compare_raw_files(FileAttrP *file1, FileAttrP *file2) {
	__int64 size_to_read = file1->size;
	bool comp_hash1 = file1->hash == 0 ? true : false;
	bool comp_hash2 = file2->hash == 0 ? true : false;
	bool is_same = true;

	char buf1[BUF_SIZE];
	char buf2[BUF_SIZE];


	FILE *f1, *f2;

	if ((f1 = fopen(file1->path, "r")) == NULL) {
		printf("could not open file1 %s \n", (char *)file1->path);
		return false;
	}

	if ((f2 = fopen(file2->path, "r")) == NULL) {
		printf("could not open file2 %s \n", (char *)file2->path);
		perror("error");
		return false;
	}

	while (size_to_read) {
		int size = size_to_read < BUF_SIZE ? size_to_read : BUF_SIZE;
		size_to_read -= size;

		if (is_same || comp_hash1) {
			fread(buf1, size, 1, f1);
			if (comp_hash1) {
				file1->hash = +file_hash(buf1);
			}
		}
		if (is_same || comp_hash2) {
			fread(buf2, size, 1, f2);
			if (comp_hash2) {
				file2->hash = +file_hash(buf2);
			}
		}

		if (is_same && memcmp(buf1, buf2, size) != 0) {
			is_same = false;
		}

		if (is_same == false && comp_hash1 == false && comp_hash2 == false)
			break;


	}
	fclose(f1);
	fclose(f2);
	return is_same;
}

/* return 0, if two files are the same*/
bool compare_files(FileAttrP *file1, FileAttrP *file2) {
	if (file1->size != file2->size) return false;
	if (file1->hash && file2->hash && file1->hash != file2->hash) return false;

	return compare_raw_files(file1, file2);
}

void traverse(FileAttrP *head) {
	FileAttrP *pre_next = head;
	FileAttrP *next = head->next;

	while (next != NULL) {
		if (compare_files(head, next)) {
			pre_next->next = next->next;
			printf("file %s %s are same\n", head->path, next->path);
			savedSpace += next->size;
			num_dup++;

			if (delete_option)
				remove(next->path);
			
			free_fileAttrP(next);
		}
		else pre_next = next;

		next = pre_next->next;		
	}
}

void print_summary_ht(FileAttrP **ht, int size) {
	FileAttrP *next, *rest_next;
	FileAttrP *current, *rest_current;

	for (int i = 0; i < size; i++) {
		int count = 0;
		current = ht[i];
		
		while (current != NULL) {
			traverse(current);
			next = current->next;
			count++;
			current = next;
		}
	}
}

void traverse_dir(char *dir, FileAttrP **ht) {
	printf("Change to %s\n", dir);
	if (_chdir(dir)) {
		printf("Unable to locate the directory: %s\n", dir);
		return;
	}

	FileAttr c_file;
	long   hFile;

	hFile = _findfirst("*.*", &c_file);
	bool next = hFile;
	while (next) {
		if (strcmp(c_file.name, ".") && strcmp(c_file.name, "..")) {
			if (c_file.attrib & _A_SUBDIR) {
				char new_path[MAX_PATH];
				if (strlen(dir) + strlen(c_file.name) < MAX_PATH) {
					sprintf(new_path, "%s\\%s", dir, c_file.name);
					traverse_dir(new_path, ht);
				}
			}
			else {
				insert_ht(ht, &c_file, dir);
				num_files++;
			}
		}
		next = _findnext(hFile, &c_file) == 0 ? true : false;
	}

	_findclose(hFile);
}



int main()
{
	char path[MAX_PATH];
	printf("please enter the directory (up to 512 characters) to inspect\n");
	scanf("%s", path);

	char options[MAX_PATH];
	printf("Do you want to delete the duplicates (Yes:No):\n");
	scanf("%s", options);
	if (strcmp(options, "Yes")) {
		delete_option = false;
	}
	else delete_option = true;

	FileAttrP *ht[HashTableSize];

	init_ht(ht, HashTableSize);
	traverse_dir(path, ht);
	print_summary_ht(ht, HashTableSize);

	printf("For the directory of %s :\n", path);
	printf(" total number of files: %I64d, space occupied: %f\n", num_files, (float)(totalSpace >> 20) / 1024);
	printf(" total number of duplicates: %I64d, space occupied: %f \n", num_dup, (float)(savedSpace >> 20) / 1024);

	clean_ht(ht, HashTableSize);
}

