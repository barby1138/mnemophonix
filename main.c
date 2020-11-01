#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
 
#include "ffmpeg.h"
#include "fingerprinting.h"
#include "fingerprintio.h"
#include "lsh.h"
#include "search.h"

static long time_in_milliseconds() {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int main(int argc, char* argv[]) {
    if (argc < 2
        //|| (strcmp(argv[1], "index") && strcmp(argv[1], "search"))
        || (strcmp(argv[1], "index") && strcmp(argv[1], "search") && strcmp(argv[1], "daemon"))
        || (!strcmp(argv[1], "search") && argc != 4)) {

        fprintf(stderr, "\n");
        fprintf(stderr, " ---                                                       ---\n");
        fprintf(stderr, " \\  \\  mnemophonix - A simple audio fingerprinting system  \\  \\\n");
        fprintf(stderr, " O  O                                                      O  O\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "%s index <input>\n", argv[0]);
        fprintf(stderr, "  Prints to stdout the index data generated for the given input file. Since\n");
        fprintf(stderr, "  the index file format is a text one, you can create a database containing\n");
        fprintf(stderr, "  multiple indexes like this:\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "  $ %s index song1.mp3 > db\n", argv[0]);
        fprintf(stderr, "  $ %s index song2.wav >> db\n", argv[0]);
        fprintf(stderr, "  $ %s index movie.mp4 >> db\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "%s search <input> <index>\n", argv[0]);
        fprintf(stderr, "  Looks for the given input file in the given index file\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "The input file format that this code can process is 44100Hz 16-bit PCM.\n");
        fprintf(stderr, "If it is not the case, an attempt will be made to generate such a file using\n");
        fprintf(stderr, "ffmpeg. Because ** ffmpeg rocks **, you can use this program with pretty\n");
        fprintf(stderr, "much any audio or video file !\n");
        fprintf(stderr, "\n");
        return 1;
    }
    char* input = argv[2];

    struct signatures* fingerprint = NULL;
    char* artist = NULL;
    char* track_title = NULL;
    char* album_title = NULL;

/////////////////////
    if (!strcmp(argv[1], "daemon")) {
        printf("Daemon mode\n");
        const char* index = argv[2];
        struct index* database_index;
        printf("Loading database %s...\n", index);
        long before_loading_db = time_in_milliseconds();
        int res = read_index(index, &database_index);
        if (res != SUCCESS) {
            switch (res) {
                case CANNOT_READ_FILE: fprintf(stderr, "Cannot read file '%s'\n", index); return 1;
                case MEMORY_ERROR: fprintf(stderr, "Memory allocation error\n"); return 1;
            }
        }
        long after_loading_db = time_in_milliseconds();
        printf("(raw database loading took %ld ms)\n", after_loading_db - before_loading_db);

        struct lsh* lsh = create_hash_tables(database_index);
        long after_lsh = time_in_milliseconds();
        printf("(lsh index building took %ld ms)\n", after_lsh - after_loading_db);

	    int fd, cl, rc;
	    struct sockaddr_un addr;
	    char buf[100], msg[100];
	    const char *socket_path = "/tmp/mnemo_tsis.sock";
	
	    if ( (fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		    perror("socket error");
    		exit(-1);
  	    }

  	    memset(&addr, 0, sizeof(addr));
  	    addr.sun_family = AF_UNIX;
    	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);
    	unlink(socket_path);

  	    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    		perror("bind error");
    		exit(-1);
  	    }

  	    if (listen(fd, 5) == -1) {
    		perror("listen error");
    		exit(-1);
  	    }

	    long fp_begin = 0;
  	    while (1) {
    		if ( (cl = accept(fd, NULL, NULL)) == -1) {
      			perror("accept error");
      			continue;
    		}
		    write(cl,"READY\n", 6);

    		while ( (rc=read(cl,buf,sizeof(buf))) > 0) {
                fingerprint = NULL;
                artist = NULL;
                track_title = NULL;
                album_title = NULL;

      			printf("read %u bytes: %.*s\n", rc, rc, buf);
			    buf[rc-1] = '\0';
			    printf("trying file %s ...\n", buf);
		
			    fp_begin = time_in_milliseconds();	
    			int res = generate_fingerprint(buf, &fingerprint, &artist, &track_title, &album_title);
    			if (res != SUCCESS) {
				    switch (res) {
				    case CANNOT_READ_FILE: fprintf(stderr, "Cannot read file '%s'\n", buf); break;
				    case MEMORY_ERROR: fprintf(stderr, "Memory allocation error\n"); break;
				    case DECODING_ERROR: fprintf(stderr, "Cannot decode file '%s'\n", buf); break;
				    case FILE_TOO_SMALL: fprintf(stderr, "'%s' is too small to generate a fingerprint\n", buf); break;
				    case UNSUPPORTED_WAVE_FORMAT:
				    case NOT_A_WAVE_FILE: {
					    // Not a wave file ? Let's try to convert it to a wave file
					    // with ffmpeg
					    char* generated_wav = generate_wave_file(buf, &artist, &track_title, &album_title);
					    if (generated_wav == NULL) {
					        fprintf(stderr, "'%s' is not a wave file and we could not convert it to one with fffmpeg\n", buf);
					        break;
					    }
					    res = generate_fingerprint(generated_wav, &fingerprint, NULL, NULL, NULL);
					    remove(generated_wav);
					    free(generated_wav);
					    switch (res) {
				    	    case MEMORY_ERROR: fprintf(stderr, "Memory allocation error\n"); break;
					        case FILE_TOO_SMALL: fprintf(stderr, "'%s' is too small to generate a fingerprint\n", buf); break;
					    }
					    res = SUCCESS;
					    break;
				    }
				    }
				    if (res != SUCCESS) {
					    write(cl, "ERROR\n", 6);
					    free_signatures(fingerprint);
					    free(artist); 
                        free(track_title); 
                        free(album_title);
					    write(cl,"READY\n", 6);
					    continue;
				    }
			    }

        	    long fp_end = time_in_milliseconds();
			    sprintf(msg,"(Fingerprinting took %ld ms)\n",fp_end-fp_begin);
			    printf("%s",msg);
			    write(cl,msg,strlen(msg));

        	    long search_begin = time_in_milliseconds();
        	    printf("Searching...\n");
			    int best_match = search(fingerprint, database_index, lsh);
    		    long search_end = time_in_milliseconds();
			    sprintf(msg,"(Search took %ld ms)\n", search_end - search_begin);
        	    printf("%s",msg);
			    write(cl,msg,strlen(msg));

        	    if (best_match == NO_MATCH_FOUND) {
        		    printf("\nNo match found\n\n");
				    write(cl,"Not matched\n", 12);
        	    } else {
				    sprintf(msg,"Found match: '%s'\n", database_index->entries[best_match]->filename);
            	    printf("\n%s",msg);
				    write(cl,msg,strlen(msg));
    		    }
			    free_signatures(fingerprint);
			    free(artist); 
                free(track_title); 
                free(album_title);
			    write(cl,"READY\n", 6);
		    }
    	
            if (rc == -1) {
      		    perror("read");
      		    exit(-1);
    	    }
		    else if (rc == 0) {
      		    printf("EOF\n");
      		    close(cl);
    	    }
  	    }

  	    return 0;
    }
//////////////////////
/*
    int cc = 0;
    while(cc < 10000)
    {
*/
    int res = NOT_A_WAVE_FILE;//generate_fingerprint(input, &fingerprint, &artist, &track_title, &album_title);
    if (res != SUCCESS) {
        switch (res) {
            case CANNOT_READ_FILE: fprintf(stderr, "Cannot read file '%s'\n", input); return 1;
            case MEMORY_ERROR: fprintf(stderr, "Memory allocation error\n"); return 1;
            case DECODING_ERROR: fprintf(stderr, "Cannot decode file '%s'\n", input); return 1;
            case FILE_TOO_SMALL: fprintf(stderr, "'%s' is too small to generate a fingerprint\n", input); return 1;
            case UNSUPPORTED_WAVE_FORMAT:
            case NOT_A_WAVE_FILE: {
                // Not a wave file ? Let's try to convert it to a wave file
                // with ffmpeg
                char* generated_wav = generate_wave_file(input, &artist, &track_title, &album_title);
                if (generated_wav == NULL) {
                    fprintf(stderr, "'%s' is not a wave file and we could not convert it to one with fffmpeg\n", input);
                    return 1;
                }
                res = generate_fingerprint(generated_wav, &fingerprint, NULL, NULL, NULL);
                remove(generated_wav);
                free(generated_wav);
                switch (res) {
                case MEMORY_ERROR: fprintf(stderr, "Memory allocation error\n"); return 1;
                case FILE_TOO_SMALL: fprintf(stderr, "'%s' is too small to generate a fingerprint\n", input); return 1;
                }
                break;
            }
        }
    }
/*
    free_signatures(fingerprint);
	free(artist); 
    free(track_title); 
    free(album_title);
    cc++;
    }
*/
    int ret_value = 0;

    if (!strcmp(argv[1], "index")) {
        save(stdout, fingerprint, input, artist, track_title, album_title);
    } else {
        const char* index = argv[3];
        struct index* database_index;
        printf("Loading database %s...\n", index);
        long before_loading_db = time_in_milliseconds();
        int res = read_index(index, &database_index);
        if (res != SUCCESS) {
            switch (res) {
                case CANNOT_READ_FILE: fprintf(stderr, "Cannot read file '%s'\n", index); return 1;
                case MEMORY_ERROR: fprintf(stderr, "Memory allocation error\n"); return 1;
            }
        }
        long after_loading_db = time_in_milliseconds();
        printf("(raw database loading took %ld ms)\n", after_loading_db - before_loading_db);

        struct lsh* lsh = create_hash_tables(database_index);
        long after_lsh = time_in_milliseconds();
        printf("(lsh index building took %ld ms)\n", after_lsh - after_loading_db);

        int c = 0;
        //while(c < 10000)
        {
        printf("Searching...\n");

        after_lsh = time_in_milliseconds();
        int best_match = search(fingerprint, database_index, lsh);
        long after_search = time_in_milliseconds();
        printf("(Search took %ld ms)\n", after_search - after_lsh);

        if (best_match == NO_MATCH_FOUND) {
            printf("\nNo match found\n\n");
            ret_value  = 1;
        } else {
            printf("\nFound match: '%s'\n", database_index->entries[best_match]->filename);
            if (database_index->entries[best_match]->artist[0]) {
                printf("Artist: %s\n", database_index->entries[best_match]->artist);
            }
            if (database_index->entries[best_match]->track_title[0]) {
                printf("Track title: %s\n", database_index->entries[best_match]->track_title);
            }
            if (database_index->entries[best_match]->album_title[0]) {
                printf("Album title: %s\n", database_index->entries[best_match]->album_title);
            }
            printf("\n");
        }
        c++;
        }
        // Since we are done, the process will be terminated so there is no point
        // in cleaning up memory as all the pages will be recycled by the OS.
    }

    return ret_value;
}
