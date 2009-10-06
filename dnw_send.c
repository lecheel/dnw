#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

const char* dev = "/dev/dnwOTG";
//const char* dev = "/dev/secbulk0";

int main(int argc, char* argv[])
{
	unsigned char* file_buffer = NULL;
	if( 2 != argc )
	{
		printf("Usage: dnw_send <filename>\n");
		return 1;
	}

	int fd = open(argv[1], O_RDONLY);
	if(-1 == fd)
	{
		printf("Can not open file - %s\n", argv[1]);
		return 1;
	}

	struct stat file_stat;
	if( -1 == fstat(fd, &file_stat) )
	{
		printf("Get file size filed!\n");
		return 1;
	}	
	
	file_buffer = (char*)malloc(file_stat.st_size+10);
	if(NULL == file_buffer)
	{
		printf("malloc failed!\n");
		goto error;
	}
	if( file_stat.st_size !=  read(fd, file_buffer+8, file_stat.st_size))
	{
		printf("Read file failed!\n");
		goto error;
	}

	printf("file name : %s\n", argv[1]);
	printf("file size : %d bytes\n", file_stat.st_size);

        int fd_dev = open(dev, O_WRONLY);
	if( -1 == fd_dev)
	{
		printf("Can not open %s\n", dev);
		goto error;
	}
	
	*((unsigned long*)file_buffer) = 0xc0000000; 	//load address
	*((unsigned long*)file_buffer+1) = file_stat.st_size+10;	//file size
	unsigned short sum = 0;
	int i;
	for(i=8; i<file_stat.st_size+8; i++)
	{
		sum += file_buffer[i];
	}
	
	printf("Writing data...cksum(%04x)\n",sum);
	size_t remain_size = file_stat.st_size+8;
	size_t block_size = remain_size / 100;
//	size_t writed = 0;
	unsigned long writed = 0;
        while(remain_size>0)
	{
		size_t to_write = remain_size > block_size ? block_size:remain_size;
		if( to_write != write(fd_dev, file_buffer+writed, to_write))
		{
			printf("failed!\n");
			return 1;
                }
		remain_size -= to_write;
                writed += to_write;
		printf("\r%d%\t %d (%x) bytes     ", writed/((file_stat.st_size+8)/100), writed, writed);
		fflush(stdout);
		
        }
        // add checksum
        write(fd_dev, (void*)&sum, 2);
	printf("OK\n");
	return 0;

error:
	if(-1!=fd_dev) close(fd_dev);
	if(fd != -1) close(fd);
	if( NULL!=file_buffer )
		free(file_buffer);
	return -1;
}
