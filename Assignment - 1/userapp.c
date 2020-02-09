#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <linux/ioctl.h>

#define MAGIC_NUM 'A'
#define CHANNEL 'a'
#define ALLIGNMENT 'b'
#define SET_CHANNEL _IOW(MAGIC_NUM, CHANNEL, int32_t*)
#define SET_ALLIGNMENT _IOW(MAGIC_NUM, ALLIGNMENT, int32_t*)

void DecimalToBinary(int n)
{
    int c, k;
    for (c = 15; c >= 0; c--)
  {
    k = n >> c;
    if (k & 1)
      printf("1");
    else
      printf("0");
  }
  printf("\n");
}
 
int main()
{
    int fd;
    uint16_t chnl, align, ADC_Val;

    fd = open("/dev/adc8", O_RDWR);
    if(fd < 0) {
            printf("Cannot open device file...\n");
            return 0;
    }

    ch:
    printf("Enter the ADC input Channel (0 to 7): ");
    scanf("%d",&chnl);
    if (chnl<0 || chnl>7)
    {
        printf("Invalid Channel\n");
    	goto ch;
    }
    ioctl(fd, SET_CHANNEL, (int32_t*) &chnl);

    al:
    printf("Enter the alignment : 0 for lower bits, 1 for higher bits: ");
    scanf("%d",&align);
    if (align<0 || align>1)
    {
        printf("Invalid input\n");
    	goto al;
    }
    ioctl(fd, SET_ALLIGNMENT, (int32_t*) &align);

    //read(fd,&ADC_Val,sizeof(ADC_Val));
    read(fd,&ADC_Val,2);
    //ADC_Val = ADC_Val & 0x3ff;
    printf("ADC value = %d \n",ADC_Val);
    printf("ADC Register: ");
    DecimalToBinary(ADC_Val); 

    close(fd);
}
