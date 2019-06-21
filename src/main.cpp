#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>

#include <Util.h>

extern uint32_t VpuCamMain(APP_DATA *pAppData);

bool bExitLoop = false;

void print_usage(const char *appName)
{
	printf( "Usage : %s [options] \n"
		"  common options :\n"
		"     -s [width],[height]         : input image's size\n"
		"     -c [crop_width],[crop_height]    : crop image's size\n"
		"     -d [display_width],[display_height] : display size\n"
		"     -o [output file name]     : output file name\n"
		"	 The storage path is \"/data/\"\n"
		"     -m [deinterlace mode]     : 0 : single, 1: double\n"
		"	 The option can preview single frame or double frame\n"
		"     -D : Enable deinterlacer\n"
		"     -P : Enable preview\n"
		"     -E : Enable rate adjust\n"
		"     -h : help\n"
		" ==========================================================\n"
		, appName);
	printf("Examples\n");
	printf( " camera display :\n");
	printf("     #> %s -s 720,480 -m 0 -D -P\n", appName);
	printf("     #> %s -s 720,480 -m 1 -D -P\n", appName);
	printf("     #> %s -s 720,480 -m 0 -D -P -E\n", appName);
	printf("     #> %s -s 720,480 -m 1 -D -P -E\n", appName);
	printf("     #> %s -s 720,480 -m 0 -D -P -E\n", appName);
	printf("     #> %s -s 720,480 -m 1 -D -P -E\n", appName);
	printf("     #> %s -s 960,480 -c 720,480 -d 720,480 -o output.yuv -m 0 -D -P\n", appName);
	printf("     #> %s -s 960,480 -c 720,480 -d 720,480 -o output.yuv -m 1 -D -P\n", appName);
}

int32_t main(int32_t argc, char *argv[])
{
	int32_t iRet = 0;
	int32_t opt;
	uint32_t iRepeat = 1, iCount = 0;
	uint32_t bEnableDeinter = 0;
	char szTemp[1024];
	APP_DATA appData;

	memset(&appData, 0, sizeof(APP_DATA));

	while (-1 != (opt = getopt(argc, argv, "o:hd:s:c:d:p:m:DPE"))) {
		switch (opt) {
		case 'o':
			appData.outFileName = strdup(optarg);
			break;
		case 'h':
			print_usage(argv[0]);
			return 0;
		case 's':
			sscanf(optarg, "%d,%d", &appData.width,
					&appData.height);
			break;
		case 'c':
			sscanf(optarg, "%d,%d", &appData.cropWidth,
					&appData.cropHeight);
			break;
		case 'd':
			sscanf(optarg, "%d,%d", &appData.dpWidth,
					&appData.dpHeight);
			break;
		case 'm':
			sscanf(optarg, "%d", &appData.deinterMode);
			break;
		case 'p':
			sscanf(optarg, "%u", &appData.dpPort);
			break;
		case 'D':
			appData.bEnableDeinter = true;
			break;
		case 'P':
			appData.bEnablePreview = true;
			break;
		case 'E':
			appData.bEnableRatio = true;
			break;
		default:
			break;
		}
	}

	VpuCamMain(&appData);

	if( appData.outFileName )
		free( appData.outFileName );

	return iRet;
}
