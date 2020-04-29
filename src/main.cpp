#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <wiringPi.h>
#include "taskRadar.h"

#define DEBUG

using namespace std;

int main(int argc, char *argv[])
{
//    printf("raspbian_x4driver start to work!\n""""""\n\n");
//    for(int i = 0;i<10;i++)
//    {
//        sleep(1);
//        printf("ready for work %d s.\n",i+1);
//    }
    std::thread taskRadarThread(taskRadar);
    taskRadarThread.join();

    printf("raspbian_x4driver done.\n");
    return 0;
}
