#include <stdio.h>
#include <stdbool.h>
#include <err.h>
#include <string.h>

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
struct Args { char* output_file; bool should_list; };

enum ArgOption { Filename = 0 };

struct Args parse_arguments(int argc, char** argv)
{
    struct Args parsed  = {"default_tar", false};

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
				break;
			default:
				err(1, "Unknown option");
		}
	}

	return parsed;
}


int
main(int argc, char* argv[])
{
    struct Args args = parse_arguments(argc, argv);
	printf("Output file: %s\n", args.output_file);
	printf("Should list: %s\n", args.should_list ? "true" : "false");
}
