#include <stdio.h>
#include <stdbool.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#define NAME_LENGTH 100
#define MODE_LENGTH 8
#define UID_LENGTH 8
#define GID_LENGTH 8
#define SIZE_LENGTH 12
#define MTIME_LENGTH 12
#define CHKSUM_LENGTH 8
#define LINKNAME_LENGTH 100
#define MAGIC_LENGTH 6
#define VERSION_LENGTH 2
#define UNAME_LENGTH 32
#define GNAME_LENGTH 32
#define DEVMAJOR_LENGTH 8
#define DEVMINOR_LENGTH 8
#define PREFIX_LENGTH 155
#define TRAILING_PADDING 12
#define RECORD_SIZE 512

#define ERR -1
#define DEBUG true

/* Argument parsing */
/*
Currently supports passing options and values separately.
For example:
	-f mytar
	-t
	-f mytar -t
	-t -f mytar
	-f mytar -t -f mytar2
*/
struct Args { bool is_valid; char* output_file; bool should_list; };

enum ArgOption { Filename = 0 };

struct Args parse_arguments(int argc, char** argv)
{
    struct Args parsed = {.is_valid = false, .output_file = "default.tar", .should_list = false};
    bool last_arg_setup = false;
    enum ArgOption last_option = Filename;

    for (int i = 0; i < argc; ++i)
    {
        if (argv[i][0] == '-')
		{
            last_arg_setup = true;
	    	switch (argv[i][1])
	    	{
                case 't':
		    		parsed.should_list = true;
                    parsed.is_valid = true;
		    		break;
				case 'f':
		    		last_option = Filename;
		    		break;
				default:
		    		err(1, "Unknown flag");
			}
			continue;
	    }
		if (!last_arg_setup)
			continue;
		last_arg_setup = false;
		// Passing option values
		switch (last_option)
		{
			case Filename:
				parsed.output_file = argv[i];
                parsed.is_valid = true;
				break;
			default:
				err(1, "Unknown option");
		}
	}

	return parsed;
}

struct TarHeader
{
	char name[NAME_LENGTH + 1];
	int mode;
	int uid;
	int gid;
	int size;
	int mtime;
	int chksum;
	char typeflag;
	char linkname[LINKNAME_LENGTH + 1];
	char magic[MAGIC_LENGTH + 1];
	char version[VERSION_LENGTH + 1];
	char uname[UNAME_LENGTH + 1];
	char gname[GNAME_LENGTH + 1];
	int devmajor;
	int devminor;
	char prefix[PREFIX_LENGTH + 1];
};

bool
read_char_based(FILE* fp, char* buffer, size_t size)
{
	size_t read = fread(buffer, sizeof(buffer[0]), size, fp);
	if (read != size)
		return false;
	return true;
}

int
read_integer_based(FILE* fp, size_t size)
{
	char buffer[size];
	size_t read = fread(buffer, sizeof(buffer[0]), size, fp);
	if (read != size)
		return ERR;
	return strtol(buffer, NULL, 8);
}

char
read_char(FILE* fp)
{
	char c;
	size_t read = fread(&c, sizeof(char), 1, fp);
	if (read != 1)
		err(1, "read_char");
	return c;
}

void
read_header(FILE* fp, struct TarHeader* header)
{
	// Read name
	if (!read_char_based(fp, header->name, NAME_LENGTH))
		err(1, "read_name");
	// Read mode
	header->mode = read_integer_based(fp, MODE_LENGTH);
	if (header->mode == ERR)
		err(1, "read_mode");
	// Read uid
	header->uid = read_integer_based(fp, UID_LENGTH);
	if (header->uid == ERR)
		err(1, "read_uid");
	// Read gid
	header->gid = read_integer_based(fp, GID_LENGTH);
	if (header->gid == ERR)
		err(1, "read_gid");
	// Read size
	header->size = read_integer_based(fp, SIZE_LENGTH);
	if (header->size == ERR)
		err(1, "read_size");
	// Read mtime
	header->mtime = read_integer_based(fp, MTIME_LENGTH);
	if (header->mtime == ERR)
		err(1, "read_mtime");
	// Read chksum
	header->chksum = read_integer_based(fp, CHKSUM_LENGTH);
	if (header->chksum == ERR)
		err(1, "read_chksum");
	// Read typeflag
	header->typeflag = read_char(fp);
	// Read linkname
	if (!read_char_based(fp, header->linkname, LINKNAME_LENGTH))
		err(1, "read_linkname");
	header->linkname[LINKNAME_LENGTH] = '\0';
	// Read magic
	if (!read_char_based(fp, header->magic, MAGIC_LENGTH))
		err(1, "read_magic");
	header->magic[MAGIC_LENGTH] = '\0';
	// Read version
	if (!read_char_based(fp, header->version, VERSION_LENGTH))
		err(1, "read_version");
	header->version[VERSION_LENGTH] = '\0';
	// Read uname
	if (!read_char_based(fp, header->uname, UNAME_LENGTH))
		err(1, "read_uname");
	header->uname[UNAME_LENGTH] = '\0';
	// Read gname
	if (!read_char_based(fp, header->gname, GNAME_LENGTH))
		err(1, "read_gname");
	header->gname[GNAME_LENGTH] = '\0';
	// Read devmajor
	header->devmajor = read_integer_based(fp, DEVMAJOR_LENGTH);
	if (header->devmajor == ERR)
		err(1, "read_devmajor");
	// Read devminor
	header->devminor = read_integer_based(fp, DEVMINOR_LENGTH);
	if (header->devminor == ERR)
		err(1, "read_devminor");
	// Read prefix
	if (!read_char_based(fp, header->prefix, PREFIX_LENGTH))
		err(1, "read_prefix");
	header->prefix[PREFIX_LENGTH] = '\0';

	// Skip traling header
	char void_buffer[TRAILING_PADDING];  // will be thrown away
	if (!read_char_based(fp, void_buffer, TRAILING_PADDING))
		err(1, "read_trailing_header");
}

void print_header(struct TarHeader* header)
{
	printf("Name: %s\n", header->name);
	if (DEBUG) {
		printf("Mode: %d\n", header->mode);
		printf("UID: %d\n", header->uid);
		printf("GID: %d\n", header->gid);
		printf("Size: %d\n", header->size);
		printf("MTIME: %d\n", header->mtime);
		printf("CHKSUM: %d\n", header->chksum);
		printf("Typeflag: %c\n", header->typeflag);
		printf("Linkname: %s\n", header->linkname);
		printf("Magic: %s\n", header->magic);
		printf("Version: %s\n", header->version);
		printf("Uname: %s\n", header->uname);
		printf("Gname: %s\n", header->gname);
		printf("Devmajor: %d\n", header->devmajor);
		printf("Devminor: %d\n", header->devminor);
		printf("Prefix: %s\n", header->prefix);
	}
}

bool is_header_empty(struct TarHeader* header)
{
	// Check if the header is empty
	if (header->name[0] == '\0' && header->mode == 0 && header->uid == 0 &&
		header->gid == 0 && header->size == 0 && header->mtime == 0 &&
		header->chksum == 0 && header->typeflag == '\0' &&
		header->linkname[0] == '\0' && header->magic[0] == '\0' &&
		header->version[0] == '\0' && header->uname[0] == '\0' &&
		header->gname[0] == '\0' && header->devmajor == 0 &&
		header->devminor == 0 && header->prefix[0] == '\0') {
			return true;
	}
	return false;
}

bool
is_end_of_archive(FILE* fp, struct TarHeader* header)
{
	// End of archive is defined by the standard as two consecutive empty records (headers)
	if (is_header_empty(header)) {
		if (DEBUG) {
			printf("First record empty\n");
		}
		// The first record is empty, check the next one
		read_header(fp, header);
		if (is_header_empty(header)) {
			if (DEBUG) {
				printf("Second record empty\n");
			}
			// The second record is also empty, we reached the end of the archive
			return true;
		}
		// The second record is not empty
		// Print the first archive to the console
		if (DEBUG) {
			printf("Second record NOT empty\n");
		}
		struct TarHeader empty_header = {0};
		print_header(&empty_header);
		// The second record not empty and currently set to header
	}
	return false;
}

void
list_archive(FILE* fp)
{
	fseek(fp, 0, SEEK_SET);
	struct TarHeader read;
	
	while (read_header(fp, &read), !is_end_of_archive(fp, &read))
	{
		if (DEBUG) {
			printf("**********\n");
		}
		print_header(&read);
		// Skip file data and advance to the next header
		size_t data_record_amount = (RECORD_SIZE - 1 + read.size) / RECORD_SIZE;  // defined by standard
		if (DEBUG) {
			printf("\tData record amount: %zu\n", data_record_amount);
		}
		fseek(fp, RECORD_SIZE * data_record_amount, SEEK_CUR);
	}

	if (DEBUG) {
		printf("**********\n");
		printf("End of archive\n");
	}
}

int
main(int argc, char* argv[])
{
    struct Args args = parse_arguments(argc, argv);
    if (!args.is_valid) {
        printf("%s\n", "Provided arguments are not valid");
	}

	if (DEBUG) {
		printf("Output file: %s\n", args.output_file);
		printf("Should list: %s\n", args.should_list ? "true" : "false");
		printf("**********\n");
	}

    FILE* fp;
	if ((fp = fopen(args.output_file, "r")) == NULL)
		err(1, "fopen");

	if (args.should_list)
		list_archive(fp);
}
