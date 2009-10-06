#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

int file_length(char *fname)
{
    int pos;
    int end;
    FILE *fp;

    if( (fp = fopen(fname, "rb")) == NULL ) {
        printf("FILE Error %s",fname);
        exit(0);
    }
    pos = ftell (fp);
    fseek (fp, 0, SEEK_END);
    end = ftell (fp);
    fseek (fp, pos, SEEK_SET);
    fclose(fp);
    return end;
}


int writeCS(char *fname, int cs) {
    FILE *fp;
    if( (fp = fopen(fname, "wb")) != NULL ) {
        {
            fwrite(&cs, 2, 1, fp );
            fclose(fp);
        }
    }
}

int readin(char *INFILE,char *fname) {
    FILE *fp;
    char ch;
    int flen=0;
    int i=0;
    unsigned int checksum=0;

    flen = file_length(INFILE);
    if( (fp = fopen(INFILE, "rb")) == NULL ) {
        printf("FILE Error %s",INFILE);
    }

    for (i=0;i<flen;i++) {
        ch = fgetc(fp);
        checksum = (checksum + ((unsigned int)ch&0xff));
    }

    fclose(fp);
    printf("Checksum:%0x %d\n",checksum&(0xffff),checksum);
    writeCS(fname,checksum&(0xffff));
    return (checksum&(0xffff));
}

int main(int argc, char **argv)
{
    int ck = 0;
    if (argc>2) {
        ck = readin(argv[1],argv[2]);
        return ck;
    } else {
        printf("u8checksum foo.bin cs\n");
        return 0;
    }
}

