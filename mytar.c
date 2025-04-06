#include <stdio.h>
#include <stdbool.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#define NAME_LENGTH 100
#define SIZE_OFFSET 124
#define SIZE_LENGTH 12

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
	size_t size;
};


struct TarHeader
read_header(FILE* fp)
{
	struct TarHeader header;
    // reset fp for good measure
    fseek(fp, 0, SEEK_SET);
    
	//read name
	char name[NAME_LENGTH + 1] = {0};
    size_t read = fread(name, sizeof(name[0]), NAME_LENGTH, fp);
    if (read != NAME_LENGTH)
	{
		err(1, "Error reading name");
	}
	strcpy(header.name, name); // note: safe - buffer[100] is always a null byte
	//read length
	char size[SIZE_LENGTH + 1] = {0};
	fseek(fp, SIZE_OFFSET - NAME_LENGTH, SEEK_CUR);
	read = fread(size, sizeof(char), SIZE_LENGTH, fp);
	if (read != SIZE_LENGTH)
	{
		err(1, "Error reading size");
	}
	header.size = (size_t)strtoull(size, NULL, 8);
	return header;
}

int
main(int argc, char* argv[])
{
    struct Args args = parse_arguments(argc, argv);
    if (!args.is_valid)
    {
        printf("%s\n", "Provided arguments are not valid");
    }
	printf("Output file: %s\n", args.output_file);
	printf("Should list: %s\n", args.should_list ? "true" : "false");

    FILE* fp;
	if ((fp = fopen(args.output_file, "r")) == NULL)
	{
		err(1, "fopen");
	}
    struct TarHeader header = read_header(fp);

	printf("Filename: %s\n", header.name);
	printf("Size: %zu\n", header.size);
}
