#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#pragma pack(push,1)
typedef struct DirEntry {
  unsigned char  DIR_Name[11];      // File name
  unsigned char  DIR_Attr;          // File attributes
  unsigned char  DIR_NTRes;         // Reserved
  unsigned char  DIR_CrtTimeTenth;  // Created time (tenths of second)
  unsigned short DIR_CrtTime;       // Created time (hours, minutes, seconds)
  unsigned short DIR_CrtDate;       // Created day
  unsigned short DIR_LstAccDate;    // Accessed day
  unsigned short DIR_FstClusHI;     // High 2 bytes of the first cluster address
  unsigned short DIR_WrtTime;       // Written time (hours, minutes, seconds
  unsigned short DIR_WrtDate;       // Written day
  unsigned short DIR_FstClusLO;     // Low 2 bytes of the first cluster address
  unsigned int   DIR_FileSize;      // File size in bytes. (0 for directories)
} DirEntry;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct BootEntry {
  unsigned char  BS_jmpBoot[3];     // Assembly instruction to jump to boot code
  unsigned char  BS_OEMName[8];     // OEM Name in ASCII
  unsigned short BPB_BytsPerSec;    // Bytes per sector. Allowed values include 512, 1024, 2048, and 4096
  unsigned char  BPB_SecPerClus;    // Sectors per cluster (data unit). Allowed values are powers of 2, but the cluster size must be 32KB or smaller
  unsigned short BPB_RsvdSecCnt;    // Size in sectors of the reserved area
  unsigned char  BPB_NumFATs;       // Number of FATs
  unsigned short BPB_RootEntCnt;    // Maximum number of files in the root directory for FAT12 and FAT16. This is 0 for FAT32
  unsigned short BPB_TotSec16;      // 16-bit value of number of sectors in file system
  unsigned char  BPB_Media;         // Media type
  unsigned short BPB_FATSz16;       // 16-bit size in sectors of each FAT for FAT12 and FAT16. For FAT32, this field is 0
  unsigned short BPB_SecPerTrk;     // Sectors per track of storage device
  unsigned short BPB_NumHeads;      // Number of heads in storage device
  unsigned int   BPB_HiddSec;       // Number of sectors before the start of partition
  unsigned int   BPB_TotSec32;      // 32-bit value of number of sectors in file system. Either this value or the 16-bit value above must be 0
  unsigned int   BPB_FATSz32;       // 32-bit size in sectors of one FAT
  unsigned short BPB_ExtFlags;      // A flag for FAT
  unsigned short BPB_FSVer;         // The major and minor version number
  unsigned int   BPB_RootClus;      // Cluster where the root directory can be found
  unsigned short BPB_FSInfo;        // Sector where FSINFO structure can be found
  unsigned short BPB_BkBootSec;     // Sector where backup copy of boot sector is located
  unsigned char  BPB_Reserved[12];  // Reserved
  unsigned char  BS_DrvNum;         // BIOS INT13h drive number
  unsigned char  BS_Reserved1;      // Not used
  unsigned char  BS_BootSig;        // Extended boot signature to identify if the next three values are valid
  unsigned int   BS_VolID;          // Volume serial number
  unsigned char  BS_VolLab[11];     // Volume label in ASCII. User defines when creating the file system
  unsigned char  BS_FilSysType[8];  // File system type label in ASCII
} BootEntry;
#pragma pack(pop)

//int validate(){
//	if (argc == 1 || int fd = open(argv[1], O_RDWR) == -1){
//                printf("Usage: ./nyufile disk <options>\n");
//                printf("  -i                     Print the file system information.\n");
//                printf("  -l                     List the root directory.\n");
//                printf("  -r filename [-s sha1]  Recover a contiguous file.\n");
//                printf("  -R filename -s sha1    Recover a possibly non-contiguous file.\n");
//                return 0;
//        }
//}

int entryPrinter(unsigned int attr, unsigned char* entryName, unsigned int fileSize, unsigned int clusterNum){
	char filename[13] = {0};
	int folderCondition = 0;
	int emptyCondition = 0;
	int nameIndex = 0;
	int dotIndex;
	//printing entry name and extension
	for (int i = 0; i < 11; i++){
		if (entryName[i] == '\n' || entryName[i] == ' '){
			continue;
		}
		if(i == 8){
			dotIndex = nameIndex;
			filename[dotIndex] = '.';
			nameIndex++;
		}
		filename[nameIndex] = entryName[i]; 
		nameIndex++;
	}
	
	if (attr & 0x10){
		filename[nameIndex] = '/';
		filename[nameIndex+1] = '\0';
		printf("%s (starting cluster = %d)\n", filename, clusterNum); 
	}
	else if (!fileSize){
		filename[nameIndex+1] = '\0';
		printf("%s (size = 0)\n", filename);
	}
	else{
		filename[nameIndex+1] = '\0';
	       	printf("%s (size = %d, starting cluster = %d)\n",filename, fileSize, clusterNum);
	}
	return 0;
}


int main(int argc, char* argv[]){
	int fd = open(argv[1], O_RDWR);
	if (argc == 1 || fd == -1){
		printf("Usage: ./nyufile disk <options>\n");
		printf("  -i                     Print the file system information.\n");
		printf("  -l                     List the root directory.\n");
		printf("  -r filename [-s sha1]  Recover a contiguous file.\n");	
		printf("  -R filename -s sha1    Recover a possibly non-contiguous file.\n");
		return 1;
	}
	
	struct stat sb;
	if (fstat(fd, &sb) == -1) perror("couldn't get file size.\n");
//	// Map file into memory
	void *addr = mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) perror("mapping failed.\n");
	
	BootEntry * diskBootArr = (BootEntry * ) addr;	
	DirEntry * diskDirArr = (DirEntry * ) addr;	

	unsigned int sizeOfReserved = diskBootArr->BPB_RsvdSecCnt * diskBootArr->BPB_BytsPerSec;
	unsigned int numFATs = diskBootArr->BPB_NumFATs;
	unsigned int bytesPerCluster = diskBootArr->BPB_BytsPerSec * diskBootArr->BPB_SecPerClus;
	
	unsigned int ** FATs = malloc(sizeof(unsigned int *) * numFATs);
	
	FATs[0] = (unsigned int *) (addr + sizeOfReserved);
	
	for (int i = 1; i < numFATs; i++){
		FATs[i] = (unsigned int *) (FATs[i - 1] + (diskBootArr->BPB_FATSz32 * diskBootArr->BPB_BytsPerSec));	
	}

	
	if (!strcmp(argv[2],"-i")){

		// Get file size
		printf("Number of FATs = %d\n",diskBootArr->BPB_NumFATs);
		printf("Number of bytes per sector = %d\n",diskBootArr->BPB_BytsPerSec);
		printf("Number of sectors per cluster = %d\n",diskBootArr->BPB_SecPerClus);	
		printf("Number of reserved sectors = %d\n",diskBootArr->BPB_RsvdSecCnt);		
	}
	else if (!strcmp(argv[2],"-l")){
		int entryCount = 0;
		unsigned int rootCluster = (unsigned int) diskBootArr->BPB_RootClus;
		unsigned int currCluster = rootCluster;
		if(FATs[0][rootCluster] == 0){
			printf("Total number of entries = %d\n", entryCount); 
			return 0;
		}
		//LISTING root dir
		while(1){			
			if(currCluster == 0x0ffffff7){
				printf("Total number of entries = %d\n", entryCount); 
				break;
			}
			unsigned int addend = sizeOfReserved + (numFATs * diskBootArr->BPB_FATSz32 * diskBootArr->BPB_BytsPerSec) + ((currCluster - 2) * bytesPerCluster); 
			
			unsigned int *clusterAddress = (unsigned int*) (addr + addend);
			DirEntry * cluster = (DirEntry * ) clusterAddress;	
			
			unsigned int sectorsPerClus = diskBootArr->BPB_SecPerClus;

			for(int j = 0; j < bytesPerCluster/32; j++){
				unsigned int fileSize = cluster[j].DIR_FileSize;
				unsigned int clusterNum = (cluster[j].DIR_FstClusHI) + cluster[j].DIR_FstClusLO;
				unsigned int attributes = (unsigned int) cluster[j].DIR_Attr;	
				if (cluster[j].DIR_Name[0] != 0xE5 && cluster[j].DIR_Name[0] != 0x00){
					entryPrinter(attributes,cluster[j].DIR_Name,fileSize,clusterNum);//PRINTING ENTRIES
					entryCount++;

				}else{
					break;
				}
			}
			if (FATs[0][currCluster] >= 0x0ffffff8){
				printf("Total number of entries = %d\n", entryCount);
				break;
			}
			else{
				currCluster = FATs[0][currCluster];
			}
		}
	}
	return 0;
}
