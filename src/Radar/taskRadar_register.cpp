#include <iostream>
#include <thread>
#include <unistd.h>
#include <mutex>
#include <condition_variable>
#include <stdio.h>
#include <string.h>
#include <wiringPi.h>
#include <wiringPiSPI.h>
#include "radar_hal.h"
#include "taskRadar.h"
#include "x4driver.h"
#include "xep_hal.h"
#include <linux/input.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include <sys/types.h>
#include <stdio.h>
#include <sys/socket.h>
//#include <linux/in.h>
#include <arpa/inet.h>
#include<unistd.h>
#include <stdlib.h>
#include <pthread.h>

int g_readDone = 0;
int g_antNum = 0;
int _Connect(char* ip, uint16_t port)
{
    int sockfd = -1;
    if( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        return (-1);
    }

    char * destaddr = ip;

    struct sockaddr_in    servaddr;
    bzero(&servaddr,sizeof(struct sockaddr_in));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr= inet_addr(destaddr);
    servaddr.sin_port = htons(port);

    if( connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0)
    {
	close(sockfd);
        //printf("connect radar failed.\n");
        return -2;
    }


    //printf("connect radar ok.\n");

    return sockfd;

}

int _Send(int sockfd, char* context, int len)
{
    if(sockfd<0)
        return -1;

    struct timeval tv;
    // timeout setting
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    fd_set fdw;
    FD_ZERO(&fdw);
    fd_set fde;
    FD_ZERO(&fde);
    fd_set fdr;
    FD_ZERO(&fdr);
    FD_SET(sockfd, &fdr);
    FD_SET(sockfd, &fdw);
    FD_SET(sockfd, &fde);
    int ret = select(sockfd + 1, &fdr, &fdw, &fde, &tv);
    if(ret<0)
    {
        printf("select failed.\n");
        printf("close socket.\n");
        return -1;
    }
    if(FD_ISSET(sockfd, &fde))
    {
        printf("select socket exception.\n");
        printf("close socket.\n");
        return -1;
    }

    if(FD_ISSET(sockfd, &fdr))
    {
        char recvbuf[256];
        int nrecv = recv(sockfd, recvbuf, 256, 0);
        if(nrecv==0)
        {
            printf("disconnect by server.\n");
            return -1;
        }
        if(nrecv<0)
        {
            printf("disconnect by server[%d].\n", nrecv);
            return -1;
        }
    }

    if(!FD_ISSET(sockfd, &fdw))
    {
        return -1;
    }

    int nsend = send(sockfd, context, len, 0);
    if( nsend <= 0)
    {
        return -1;
    }
    //printf("send ssid to radar data [%d] ok.\n", len);


    return 0;
}


class CDataCache
{
public:
    CDataCache();
    ~CDataCache();
public:
    void Add(char* buf, int size);
    int Get(char* outbuf, int size);
    void Clear();

private:
    int lock();
    void unlock();

private:
    char*       m_buf;
    int         m_size;
    int         m_MAXSIZE;

    char        m_busy; //lock
};

CDataCache::CDataCache()
{
    m_MAXSIZE = 1024*1024;
    m_buf = new char[m_MAXSIZE];
    m_size = 0;
    m_busy = 0;
}
CDataCache::~CDataCache()
{
    delete[] m_buf;
}

void CDataCache::Add(char *buf, int size)
{
    lock();
    if(size + m_size>m_MAXSIZE)
    {
        unlock();
        return;
    }
    memcpy(m_buf + m_size, buf, size);
    m_size += size;
    unlock();
    //printf("add radar data [%d]\n", size);
}

int CDataCache::Get(char *outbuf, int size)
{
    lock();
    if(m_size==0)
    {
        unlock();
        return 0;
    }
    if(size>m_size)
        size = m_size;

    memcpy(outbuf, m_buf, size);
    if(m_size>size)
    {
        memmove(m_buf, m_buf+size, m_size-size);
        m_size -= size;
    }
    else
    {
        m_size = 0;
    }

    unlock();

    return size;
}

void CDataCache::Clear()
{
    lock();
    m_size = 0;
    unlock();
}

int CDataCache::lock()
{
    while(m_busy)
    {
        usleep(1000);
    }
    m_busy = 1;

    return 0;
}

void CDataCache::unlock()
{
    m_busy = 0;
}



class CSocketSender
{
public:
    CSocketSender(char* ip, int port);
    ~CSocketSender();

public:
    void Send(char* buf, int size);
private:
    static void* sendthread(void* param);
    void sendfun();
private:
    int m_sockfd;
    CDataCache m_cache;
    pthread_t   m_ntidsend;

    char    m_ip[32];
    int     m_port;
};


CSocketSender::CSocketSender(char* ip, int port)
{
    strcpy(m_ip, ip);
    m_port = port;
    m_sockfd = 0;
    pthread_create(&m_ntidsend, 0, sendthread, this);
}
CSocketSender::~CSocketSender()
{
    if(m_sockfd)
        close(m_sockfd);
}

void CSocketSender::Send(char* buf, int size)
{
    m_cache.Add(buf, size);
}

void* CSocketSender::sendthread(void* param)
{
    CSocketSender* pthis = (CSocketSender*)param;
    pthis->sendfun();
}

void CSocketSender::sendfun()
{
    char sendbuf[8192];
    while(1)
    {
        if(m_sockfd<=0)
            m_sockfd = _Connect(m_ip, m_port);
        if(m_sockfd<=0)
        {
            usleep(1000000);
            continue;
        }

        int size = m_cache.Get(sendbuf, 8192);
        if(size<=0)
        {
            usleep(1000);
            continue;
        }

        if(_Send(m_sockfd, sendbuf, size)<0)
        {
	    close(m_sockfd);
            m_sockfd = 0;
	    usleep(1000*500);
        }
    }
}


CSocketSender* psender = new CSocketSender("192.168.31.201", 15648);
//CSocketSender* psender2 = new CSocketSender("192.168.31.218", 20000);



#pragma pack(1)
typedef struct _X4FRAMEDATA
{
//    int frameno;
//    int bincnt;
    int fall;//1=fall,0=no fall
    float* bins;

    _X4FRAMEDATA()
    {
//        frameno = 0;
//        bincnt = 0;
        fall = 0;
        bins = 0;
    }

    ~_X4FRAMEDATA()
    {
//        bincnt = 0;
        if(bins)
            delete[] bins;
        bins = 0;
        fall = 0;
    }

} tagX4FrameData;

typedef struct _X4FRAMEDATA2
{
    char head[16];
    char radarid[16];
    uint32_t antNum;
    uint32_t frameno;
    uint32_t bincnt;
    float bins[138*2];

    _X4FRAMEDATA2()
    {
        strcpy(head, "xglx4dataframe");
	//strcpy(radarid, "0000001");
        frameno = 0;
        bincnt = 138;
    }

    ~_X4FRAMEDATA2()
    {
    }

} tagX4FrameData2;

#pragma pack()

#pragma pack(1)
typedef struct X4_DATA_BUFFER
{
    tagX4FrameData* x4data;
    int buffer_size;
    int read_ptr;
    int write_ptr;

    X4_DATA_BUFFER()
    {
        read_ptr = 0;
        write_ptr = 0;
    }

    ~X4_DATA_BUFFER()
    {
        if(x4data)
            delete[] x4data;
    }
}x4_data_buffer_t;
#pragma pack()


class X4Cache
{
private:
    tagX4FrameData*		m_frameCache;
    int               m_curDataCnt;
    int               m_maxDataCnt;
    char              m_timeflag[16];
    int                m_count;
    x4_data_buffer_t* m_x4_buf;
    const int FS = 400;
    int m_bt_evn_cnt;
//    int bt_count;
//    FILE* m_fnfid;
public:
    char fname[100];
    long file_size = 0;
//    char frameno_fname[100];
    pthread_t saveid;
    pthread_t btid;
    bool bt_evn;
    FILE* fid;
    bool okflag;
    int okcnt;

private:
    int InitCache()
    {
        int fs = FS;
        int secs = 10;
        int cachesize = fs*secs;
        tagX4FrameData* newbuf = new tagX4FrameData[cachesize];
        m_frameCache = newbuf;
        m_maxDataCnt = cachesize;
        m_curDataCnt = 0;
        strcpy(m_timeflag ,"00000000_000000");
        m_count =0;
        x4_data_buffer_t* x4_buf = new x4_data_buffer_t();
        tagX4FrameData* x4_frams = new tagX4FrameData[cachesize];
        x4_buf->x4data = x4_frams;
        x4_buf->buffer_size = cachesize;
        m_x4_buf = x4_buf;
		time_t timep;
		time(&timep);
		char tmp[16];
		memset(tmp,0,sizeof(tmp));
		strftime(tmp, sizeof(tmp), "%Y%m%d_%H%M%S", localtime(&timep));
		sprintf(fname, "%s%s%s","data/",tmp,".dat");
//		sprintf(frameno_fname, "%s%s%s","data/",tmp,"_fn.dat");
		bt_evn = false;
		m_bt_evn_cnt = 1;
		file_size = 0;
		okflag = false;
		okcnt = 0;
//		bt_count = 0;
//		m_fnfid = fopen(frameno_fname, "wb");
//		if(m_fnfid==NULL)
//        {
//            printf("create fnfile error\n");
//        }
        return 0;
    }

    int FreeAll()
    {
        if(m_frameCache)
            delete[] m_frameCache;
        m_frameCache = 0;
        m_curDataCnt = 0;
        m_maxDataCnt = 0;
        m_count =0;
//        fclose(m_fnfid);
    }

public:
    X4Cache()
    {
        m_frameCache = 0;
        m_curDataCnt = 0;
        m_maxDataCnt = 0;
        InitCache();
    }
    ~X4Cache()
    {
    	FreeAll();
    }

public:
    void AddData(int antNum, int frameno, float* data, int cnt)
    {
        if(cnt<=0)
            return;
        //if(m_curDataCnt>=m_maxDataCnt)
        //{
        //    printf("data cache full.\n");
        //    return;
        //}

        if(cnt>138*2)
        	cnt = 138*2;

	      tagX4FrameData2 fd;
        tagX4FrameData2* pFrame = &fd;
        pFrame->frameno = htonl(frameno);
        pFrame->bincnt = 138;
        pFrame->antNum = antNum;
        memcpy(pFrame->bins, data, cnt*sizeof(float));
	printf("frame:%d\n", frameno);
        psender->Send((char*)pFrame, sizeof(tagX4FrameData2));
        //psender2->Send((char*)pFrame, sizeof(tagX4FrameData2));
    }

    void CheckSave()
    {
        const int SAVESIZE = m_maxDataCnt;
		if(m_curDataCnt % FS==0)
        {
            m_count++;
            printf("==========%d============\n",m_count);
        }
        if(m_curDataCnt<SAVESIZE)
            return;
        char s[100];
		time_t timep;
		time(&timep);
		char tmp[16];
		memset(tmp,0,sizeof(tmp));
		strftime(tmp, sizeof(tmp), "%Y%m%d_%H%M%S", localtime(&timep));
		sprintf(s, "%s%s%s","FallDataSet/",tmp,".dat");
        FILE* fid = fopen(s, "wb");
        //FILE* fid = fopen("/home/pi/x4rec.dat", "wb");
        int i;
        for(i=0;i<m_curDataCnt;i++)
        {
            tagX4FrameData* pFrame = &m_frameCache[i];
//            fwrite(&pFrame->frameno, 4, 1, fid);
//            fwrite(&pFrame->bincnt, 4, 1, fid);
            fwrite(pFrame->bins, 4, 276, fid);
        }
        fclose(fid);
        printf("================================\n");
        printf("=======%s.dat======\n",tmp);
        printf("================================\n");
        exit(0);
    }

    int buffer_write(int frameno, float* data, int cnt)
    {
//        printf("frameno:%d,cnt:%d\n",frameno,cnt);
        if(cnt<=0)
        {
            return 0;
        }

//        printf("m_x4_buf->write_ptr:%d\n",m_x4_buf->write_ptr);

        if(bt_evn)
        {

            bt_evn = false;
            okflag = true;
//            printf("btn evn\n");
         //   popen("play 98k.wav 1>/dev/null 2>&1","r");
            system("play 98k.wav 1>/dev/null 2>&1 &");
            printf("%d fall in frameno:%d\n",m_bt_evn_cnt,frameno);
//            if(m_fnfid == NULL)
//            {
//                printf("file error\n");
//            }
//            fwrite(&bt_count, 4, 1, m_fnfid);
//            fwrite(&frameno, 4, 1, m_fnfid);
            m_x4_buf->x4data[m_x4_buf->write_ptr].fall = 1;
        }
        else
        {
            m_x4_buf->x4data[m_x4_buf->write_ptr].fall = 0;
        }


        if(okflag)
        {
            okcnt ++;
            if(okcnt>2000)
            {

                okcnt = 0;
                okflag = false;

                if(m_bt_evn_cnt==20)
                {
                    char soundfname[100];
                    //printf("m_bt_evn_cnt=%d\n",m_bt_evn_cnt);
                    sprintf(soundfname, "play %d.mp3 1>/dev/null 2>&1 &",m_bt_evn_cnt);
                    //printf("before play sound cnt%d\n", m_bt_evn_cnt);
                    //popen(soundfname,"r");
                    system(soundfname);
                    m_bt_evn_cnt = 1;
                }
                else
                {
                    char soundfname[100];
                    //printf("cnt evn = %d\n", m_bt_evn_cnt);
                    sprintf(soundfname, "play %d.mp3 1>/dev/null 2>&1 &",m_bt_evn_cnt);
                    //printf("before play sound cnt %d\n", m_bt_evn_cnt);
                    system(soundfname);
//                    FILE* pid = popen(soundfname,"r");
//                    if(!pid)
//                    {
//                        printf("popen failed.\n");
//                    }
                    m_bt_evn_cnt ++;
                }
                //printf("play sound finished.\n");
            }
        }

//        m_x4_buf->x4data[m_x4_buf->write_ptr].frameno = frameno;
//        m_x4_buf->x4data[m_x4_buf->write_ptr].bincnt = cnt;
        m_x4_buf->x4data[m_x4_buf->write_ptr].bins = new float[cnt];
        memcpy(m_x4_buf->x4data[m_x4_buf->write_ptr].bins, data, cnt*sizeof(float));
        ptr_iter(m_x4_buf->write_ptr);
//        printf("%d\n",cnt);
    }

    int buffer_read_and_save()
    {
        if(fid == nullptr)
        {
            fid = fopen(fname, "wb");
            if(fid == nullptr)
            {
                printf("fid null\n");
                return -1;
            }
        }
        int write_ptr_backup = m_x4_buf->write_ptr;
        int read_sz = buffer_data_len(m_x4_buf->read_ptr,write_ptr_backup,m_x4_buf->buffer_size);
//        printf("read_sz:%d\n",read_sz);
        if(read_sz < 0)
        {
            printf("x4 data buffer empty!\n");
            return -1;
        }

        for(int i=0;i<read_sz;i++)
        {
//            fwrite(&m_x4_buf->x4data[m_x4_buf->read_ptr].frameno, 4, 1, fid);
//            fwrite(&m_x4_buf->x4data[m_x4_buf->read_ptr].bincnt, 4, 1, fid);
            file_size += fwrite(&m_x4_buf->x4data[m_x4_buf->read_ptr].fall, 4, 1, fid);
            file_size += fwrite(m_x4_buf->x4data[m_x4_buf->read_ptr].bins, 4, 276, fid);

            fflush(fid);

            if(m_x4_buf->x4data[m_x4_buf->read_ptr].bins)
            {
                delete[] m_x4_buf->x4data[m_x4_buf->read_ptr].bins;
                m_x4_buf->x4data[m_x4_buf->read_ptr].bins = 0;
            }

            ptr_iter(m_x4_buf->read_ptr);

            if(file_size >= 1024*1024*25)
            {
                printf("close file %s \n",fname);
//                printf("file_size:%l\n",file_size);
                fclose(fid);

                char s[100];
                time_t timep;
                time(&timep);
                char tmp[16];
                memset(tmp,0,sizeof(tmp));
                strftime(tmp, sizeof(tmp), "%Y%m%d_%H%M%S", localtime(&timep));
                sprintf(s, "%s%s%s","data/",tmp,".dat");
                fid = fopen(s, "wb");
                printf("create file %s \n",s);

                file_size = 0;
            }
        }
        return 0;
    }

    void save_buffer_to_file()
    {

    }

    int ptr_iter(int& ptr)
    {
        if(ptr == m_x4_buf->buffer_size -1)
        {
            ptr = 0;
        }
        else
        {
            ptr++;
        }
        return 0;
    }

    int buffer_res_len(int read,int write,int len)
    {
        if(read <= write)
        {
            int res1 = len - write;
            int res2 = read;
            return res1 + res2;
        }
        else
        {
            return read - write;
        }
    }
    int buffer_data_len(int read,int write,int len)
    {
        if(read <= write)
        {
            return write - read;
        }
        else
        {
            int len1 = len - read;
            int len2 = write;
            return len1 + len2;
        }
    }
};

X4Cache  g_frameCache;


void* save_x4data(void* handle)
{
    X4Cache* x4_buf = (X4Cache*)handle;
    x4_buf->fid = fopen(x4_buf->fname, "wb");
    x4_buf->file_size = 0;
    for(;;)
    {
        x4_buf->buffer_read_and_save();
        usleep(1000000*3);//3s
    }
    if(x4_buf->fid != NULL)
    {
        fclose(x4_buf->fid);
    }

    return 0;
}


void* bt_recv_thread(void* handle)
{

    X4Cache* x4_buf = (X4Cache*)handle;

	int keys_fd;
	struct input_event input_evn;
	keys_fd=open("/dev/input/event0", O_RDONLY);

	if(keys_fd <= 0)
	{
		printf("Bluetooth connect error!\n");
//		for(;;)
//        {
//            bt_recv_thread(void*);
//            sleep(1);
//        }
//		return 0;
	}
	else
    {
		printf("Bluetooth connected!\n");
    }

    bool key_down = false, key_up = false;
//    int bt_count = 0;
    for(;;)
    {
        int ret = NULL;
        if(keys_fd >= 0)
            ret = read(keys_fd, &input_evn, sizeof(input_evn)) == sizeof(input_evn);
        if(ret)
		{
			if(input_evn.type==EV_KEY)
			{
                if(input_evn.value==0)
                {
                    key_down = true;
                }
                if(input_evn.value==1)
                {
                    key_up = true;
                }
			}
		}
		else
        {
            printf("bluetooth retry connect...\n");
            sleep(1);
            keys_fd=open("/dev/input/event0", O_RDONLY);
            if(keys_fd <= 0)
            {
                printf("Bluetooth connect error!\n");
            }
            else
            {
                printf("Bluetooth connected!\n");
            }
        }
		if(key_down&&key_up)
        {
            key_down = false;
            key_up = false;
            x4_buf->bt_evn = true;
//            x4_buf->bt_count = x4_buf->bt_count + 1;
//            printf("%s %d \n",__FUNCTION__,++bt_count);
        }
    }

	close(keys_fd);

    return 0;
}


volatile xtx4driver_errors_t x4_initialize_status = XEP_ERROR_X4DRIVER_UNINITIALIZED;
X4Driver_t* x4driver = NULL;

#define DEBUG 0

using namespace std;
std::recursive_mutex x4driver_mutex;

typedef struct
{
    //TaskHandle_t radar_task_handle;
    radar_handle_t* radar_handle;				// Some info separating different radar chips on the same module.
} XepRadarX4DriverUserReference_t;

typedef struct
{
    //XepDispatch_t* dispatch;
    X4Driver_t* x4driver;
} RadarTaskParameters_t;


void x4driver_GPIO_init(void)
{
    wiringPiSetup();
    pinMode(X4_ENABLE_PIN, OUTPUT);
    pinMode(X4_GPIO_INT, INPUT);
    pullUpDnControl (X4_GPIO_INT, PUD_DOWN);
}

void x4driver_spi_init(void)
{
    wiringPiSPISetup(SPI_CHANNEL, 32000000);
}


void x4driver_data_ready(void)
{
    static int doprint = 0;
    doprint = doprint+1;
    bool isprint = false;
    static int recvcnt = 0;
    static float recvsize = 0;
    if(doprint>=100)
    {
	isprint = true;
	doprint = 0;
    }
    uint32_t status = XEP_ERROR_X4DRIVER_OK;
    uint32_t bin_count = 0;
    x4driver_get_frame_bin_count(x4driver,&bin_count);
    uint8_t down_conversion_enabled = 0;
    x4driver_get_downconversion(x4driver,&down_conversion_enabled);

    uint32_t fdata_count = bin_count;
    if(down_conversion_enabled == 1)
    {
        fdata_count = bin_count*2;
    }

    uint32_t frame_counter=0;
    float32_t data_frame_normolized[fdata_count];
    //sleep(1);
    status = x4driver_read_frame_normalized(x4driver,&frame_counter,data_frame_normolized,fdata_count);

    if(XEP_ERROR_X4DRIVER_OK == status)
    {
        g_frameCache.AddData(g_antNum, frame_counter,data_frame_normolized, fdata_count);
        g_frameCache.buffer_write(frame_counter,data_frame_normolized, fdata_count);
    }
    else
    {
        printf("frame error.\n");
    }

    for(int x = 0;x<fdata_count;x++)
    {
        if(data_frame_normolized[x]>0.5)
            printf("errval :%f\n", data_frame_normolized[x]);
    }
    g_readDone = 1; 
	g_antNum ++;
        if(g_antNum  > 8) 
	{
		g_antNum  = 1;
	}
    recvsize += fdata_count*sizeof(float32_t);
    recvcnt ++;
    if(!isprint)
        return;
    if(XEP_ERROR_X4DRIVER_OK == status)
    {
        //printf("x4 frame data ready! \n");

    }
    else
    {
        //printf("fail to get x4 frame data errorcode:%d! \n", status);
    }

    //printf("recv cnt=%d, recvsize=%.2f\n", recvcnt, recvsize);
    //printf("Size:%d,New Frame Data Normolized(%d){\n",fdata_count,frame_counter);
    for(uint32_t i=0; i<fdata_count; i++)
    {
        //printf(" %f, ",data_frame_normolized[i] );
    }
    //printf("}\n");
}


static uint32_t x4driver_callback_take_sem(void * sem,uint32_t timeout)
{
    x4driver_mutex.lock();
    return 1;
}

static void x4driver_callback_give_sem(void * sem)
{
    x4driver_mutex.unlock();
}


static uint32_t x4driver_callback_pin_set_enable(void* user_reference, uint8_t value)
{
    XepRadarX4DriverUserReference_t* x4driver_user_reference = (XepRadarX4DriverUserReference_t* )user_reference;
    int status = radar_hal_pin_set_enable(x4driver_user_reference->radar_handle, value);
    return status;
}

static uint32_t x4driver_callback_spi_write(void* user_reference, uint8_t* data, uint32_t length)
{
    XepRadarX4DriverUserReference_t* x4driver_user_reference = (XepRadarX4DriverUserReference_t*)user_reference;
    return radar_hal_spi_write(x4driver_user_reference->radar_handle, data, length);
}
static uint32_t x4driver_callback_spi_read(void* user_reference, uint8_t* data, uint32_t length)
{
    XepRadarX4DriverUserReference_t* x4driver_user_reference = (XepRadarX4DriverUserReference_t*)user_reference;
    return radar_hal_spi_read(x4driver_user_reference->radar_handle, data, length);
}

static uint32_t x4driver_callback_spi_write_read(void* user_reference, uint8_t* wdata, uint32_t wlength, uint8_t* rdata, uint32_t rlength)
{
    XepRadarX4DriverUserReference_t* x4driver_user_reference = (XepRadarX4DriverUserReference_t*)user_reference;
    return radar_hal_spi_write_read(x4driver_user_reference->radar_handle, wdata, wlength, rdata, rlength);
}

static void x4driver_callback_wait_us(uint32_t us)
{
    delayMicroseconds(us);
}


void x4driver_enable_ISR(void* user_reference,uint32_t enable)
{
    if(enable == 1)
    {
        pinMode(X4_GPIO_INT, INPUT);
        pullUpDnControl (X4_GPIO_INT, PUD_DOWN);
        if(wiringPiISR(X4_GPIO_INT,INT_EDGE_RISING,&x4driver_data_ready)<0)
        {
            printf("unable to setup ISR");
        }
    }
    else
        pinMode(X4_GPIO_INT, OUTPUT);//disable Interrupt
}



uint32_t task_radar_init(X4Driver_t** x4driver)
{
    x4driver_GPIO_init();
    x4driver_spi_init();

    XepRadarX4DriverUserReference_t* x4driver_user_reference = (XepRadarX4DriverUserReference_t*)malloc(sizeof(XepRadarX4DriverUserReference_t));
    memset(x4driver_user_reference, 0, sizeof(XepRadarX4DriverUserReference_t));


    void * radar_hal_memory = malloc(radar_hal_get_instance_size());
    int status = radar_hal_init(&(x4driver_user_reference->radar_handle), radar_hal_memory);

#ifdef DEBUG
    if(status == XT_SUCCESS)
    {
        printf("radar_hal_init success\n");
    }
    else
    {
        printf("radar_hal_init unknow situcation\n");
    }
#endif // DEBUG

    //! [X4Driver Platform Dependencies]

    // X4Driver lock mechanism, including methods for locking and unlocking.
    X4DriverLock_t lock;
    lock.object = (void* )&x4driver_mutex;
    lock.lock = x4driver_callback_take_sem;
    lock.unlock = x4driver_callback_give_sem;

    // X4Driver timer for generating sweep FPS on MCU. Not used when sweep FPS is generated on X4.
        uint32_t timer_id_sweep = 2;
    X4DriverTimer_t timer_sweep;
    //    timer_sweep.object = xTimerCreate("X4Driver_sweep_timer", 1000 / portTICK_PERIOD_MS, pdTRUE, (void*)timer_id_sweep, x4driver_timer_sweep_timeout);
    //    timer_sweep.configure = x4driver_timer_set_timer_timeout_frequency;

    // X4Driver timer used for driver action timeout.
    //    uint32_t timer_id_action = 3;
    X4DriverTimer_t timer_action;
    //    timer_action.object = xTimerCreate("X4Driver_action_timer", 1000 / portTICK_PERIOD_MS, pdTRUE, (void*)timer_id_action, x4driver_timer_action_timeout);
    //	timer_action.configure = x4driver_timer_set_timer_timeout_frequency;

    // X4Driver callback methods.
    X4DriverCallbacks_t x4driver_callbacks;

    x4driver_callbacks.pin_set_enable = x4driver_callback_pin_set_enable;   // X4 ENABLE pin
    x4driver_callbacks.spi_read = x4driver_callback_spi_read;               // SPI read method
    x4driver_callbacks.spi_write = x4driver_callback_spi_write;             // SPI write method
    x4driver_callbacks.spi_write_read = x4driver_callback_spi_write_read;   // SPI write and read method
    x4driver_callbacks.wait_us = x4driver_callback_wait_us;                 // Delay method
//  x4driver_callbacks.notify_data_ready = x4driver_notify_data_ready;      // Notification when radar data is ready to read
//  x4driver_callbacks.trigger_sweep = x4driver_trigger_sweep_pin;          // Method to set X4 sweep trigger pin
    x4driver_callbacks.enable_data_ready_isr = x4driver_enable_ISR;         // Control data ready notification ISR


    void* x4driver_instance_memory = malloc(x4driver_get_instance_size());//pvPortMalloc(x4driver_get_instance_size());
    //x4driver_create(x4driver, x4driver_instance_memory, &x4driver_callbacks,&lock,&timer_sweep,&timer_action, (void*)x4driver_user_reference);
    x4driver_create(x4driver, x4driver_instance_memory, &x4driver_callbacks,&lock,&timer_sweep,&timer_action, x4driver_user_reference);


#ifdef DEBUG
    if(status == XEP_ERROR_X4DRIVER_OK)
    {
        printf("x4driver_create success\n");
    }
    else
    {
        printf("x4driver_create unknow situcation\n");
    }
#endif // DEBUG

    RadarTaskParameters_t* task_parameters = (RadarTaskParameters_t*)malloc(sizeof(RadarTaskParameters_t));
    //task_parameters->dispatch = dispatch;
    task_parameters->x4driver = *x4driver;

    task_parameters->x4driver->spi_buffer_size = 192*32;
    task_parameters->x4driver->spi_buffer = (uint8_t*)malloc(task_parameters->x4driver->spi_buffer_size);
    if ((((uint32_t)task_parameters->x4driver->spi_buffer) % 32) != 0)
    {
        int alignment_diff = 32 - (((uint32_t)task_parameters->x4driver->spi_buffer) % 32);
        task_parameters->x4driver->spi_buffer += alignment_diff;
        task_parameters->x4driver->spi_buffer_size -= alignment_diff;
    }
    task_parameters->x4driver->spi_buffer_size -= task_parameters->x4driver->spi_buffer_size % 32;

//    xTaskCreate(task_radar, (const char * const) "Radar", TASK_RADAR_STACK_SIZE, (void*)task_parameters, TASK_RADAR_PRIORITY, &h_task_radar);
//    x4driver_user_reference->radar_task_handle = h_task_radar;

    // TODO: downconversion bug
    //task_parameters->x4driver->downconversion_enabled=1;

    return XT_SUCCESS;
}

void switchAnt(int rfNum)
{
	switch(rfNum)
	{
		case 1:
		{
			 
			digitalWrite(SWITCH_A, LOW);
			digitalWrite(SWITCH_B, LOW);
			digitalWrite(SWITCH_C, LOW);
			break;
		}
		case 2:
		{
			 
			digitalWrite(SWITCH_A, HIGH);
			digitalWrite(SWITCH_B, LOW);
			digitalWrite(SWITCH_C, LOW);
			break;
		}
		case 3:
		{
			 
			digitalWrite(SWITCH_A, LOW);
			digitalWrite(SWITCH_B, HIGH);
			digitalWrite(SWITCH_C, LOW);
			break;
		}
		case 4:
		{
			 
			digitalWrite(SWITCH_A, HIGH);
			digitalWrite(SWITCH_B, HIGH);
			digitalWrite(SWITCH_C, LOW);
			break;
		}
		case 5:
		{
			 
			digitalWrite(SWITCH_A, LOW);
			digitalWrite(SWITCH_B, LOW);
			digitalWrite(SWITCH_C, HIGH);
			break;
		}
		case 6:
		{
			 
			digitalWrite(SWITCH_A, HIGH);
			digitalWrite(SWITCH_B, LOW);
			digitalWrite(SWITCH_C, HIGH);
			break;
		}
		case 7:
		{
			 
			digitalWrite(SWITCH_A, LOW);
			digitalWrite(SWITCH_B, HIGH);
			digitalWrite(SWITCH_C, HIGH);
			break;
		}
		case 8:
		{
			 
			digitalWrite(SWITCH_A, HIGH);
			digitalWrite(SWITCH_B, HIGH);
			digitalWrite(SWITCH_C, HIGH);
			break;
		}
		default: break;
	}
}

#include <unistd.h>

int taskRadar(void)
{
    int xgl_iter = 16;
    int xgl_pulsestep = 1;
    int xgl_dacmin = 949;//949;
    int xgl_dacmax = 1100;//1100
    xtx4_dac_step_t xgl_dacstep = DAC_STEP_1;
    float xgl_fps = 40;
    int xgl_getiq = 1;

    printf("task_radar start!\n");

    uint32_t status = 0;
    //uint8_t* data_frame;

    //initialize radar task

    status = task_radar_init(&x4driver);


#ifdef DEBUG
    if(status == XT_SUCCESS)
    {
        printf("task_radar_init success\n");
    }
    else if(status == XT_ERROR)
    {
        printf("task_radar_init failure\n");
    }
    else
    {
        printf("task_radar_init unknow situcation\n");
    }
#endif // DEBUG

    xtx4driver_errors_t tmp_status =  (xtx4driver_errors_t) x4driver_init(x4driver);


#ifdef DEBUG
    if(tmp_status == XEP_ERROR_X4DRIVER_OK)
    {
        printf("x4driver_init success\n");
    }
    else
    {
        printf("x4driver_init unknow situcation\n");
    }
#endif // DEBUG

    status = x4driver_set_sweep_trigger_control(x4driver, SWEEP_TRIGGER_MANUAL); // By default let sweep trigger control done by X4
#ifdef DEBUG
    if(status == XEP_ERROR_X4DRIVER_OK)
    {
        printf("x4driver_set_sweep_trigger_control success\n");
    }
    else
    {
        printf("x4driver_set_sweep_trigger_control unknow situcation\n");
    }
#endif // DEBUG


//    x4_initialize_status = tmp_status;


    status=x4driver_set_dac_min(x4driver, xgl_dacmin);
    if (status != XEP_ERROR_X4DRIVER_OK)
    {
#ifdef DEBUG
        printf("Error setting dac minimum\n");
        printf("Error code=%d\n",status);
#endif
        return 1;
    }
#ifdef DEBUG
    printf("x4driver_set_dac_min success\n");
#endif
    status=x4driver_set_dac_max(x4driver, xgl_dacmax);
    if (status != XEP_ERROR_X4DRIVER_OK)
    {
#ifdef DEBUG
        printf("Error setting dac maximum\n");
        printf("Error code=%d\n",status);
#endif
        return 1;
    }
#ifdef DEBUG
    printf("x4driver_set_dac_max success\n");
#endif
    status=x4driver_set_iterations(x4driver, xgl_iter);//32);
    if (status != XEP_ERROR_X4DRIVER_OK)
    {
#ifdef DEBUG
        printf("Error in x4driver_set_iterations\n");
        printf("Error code=%d\n",status);
#endif
        return 1;

    }
#ifdef DEBUG
    printf("x4driver_set_iterations success\n");
#endif
    status=x4driver_set_pulses_per_step(x4driver, xgl_pulsestep);//140);
    if (status != XEP_ERROR_X4DRIVER_OK)
    {
#ifdef DEBUG
        printf("Error in x4driver_set_pulses_per_step\n");
        printf("Error code=%d\n",status);
#endif
        return 1;

    }
#ifdef DEBUG
    printf("x4driver_set_pulses_per_step success\n");
#endif
    status=x4driver_set_downconversion(x4driver, xgl_getiq);//1);// Radar data as downconverted baseband IQ, not RF.
    if (status != XEP_ERROR_X4DRIVER_OK)
    {
#ifdef DEBUG
        printf("Error in x4driver_set_downconversion\n");
        printf("Error code=%d\n",status);
#endif
        return 1;

    }
#ifdef DEBUG
    printf("x4driver_set_downconversion success\n");
#endif


    status=x4driver_set_frame_area_offset(x4driver, 0); // Given by module HW. Makes frame_area start = 0 at front of module.
    if (status != XEP_ERROR_X4DRIVER_OK)
    {
#ifdef DEBUG
        printf("Error in x4driver_set_frame_area_offseto\n");
        printf("Error code=%d\n",status);
#endif
        return 1;

    }
#ifdef DEBUG
    printf("x4driver_set_frame_area_offset success\n");
#endif


     status=x4driver_set_frame_area(x4driver, 0.2, 7.2); // Observe from 0.5m to 4.0m.
     if (status != XEP_ERROR_X4DRIVER_OK)
     	{
      	#ifdef DEBUG
     	printf("Error in x4driver_set_frame_area\n");
     	printf("Error code=%d\n",status);
     	#endif
     	return 1;

      	}
      printf("x4driver_set_frame_area success\n");



    status = x4driver_check_configuration(x4driver);
#ifdef DEBUG
    if(status == XEP_ERROR_X4DRIVER_OK)
    {
        printf("x4driver_check_configuration success\n");
    }
    else
    {
        printf("x4driver_check_configuration unknow situcation\n");
    }
#endif // DEBUG



/***************set fps, this will trigger data output***************/
    //status=x4driver_set_dac_step(x4driver, DAC_STEP_1);
    //status=x4driver_set_fps(x4driver, 40); // Generate 5 frames per second
   //x4driver_start_sweep(x4driver);
    if (status != XEP_ERROR_X4DRIVER_OK)
    {
//#ifdef DEBUG
        printf("Error in x4driver_set_fps\n");
        printf("Error code=%d\n",status);
//#endif
        return 1;

    }
//#ifdef DEBUG
    printf("x4driver_set_fps success\n");
//#endif

    printf("init and set ok\n");


    int sv_err = pthread_create(&g_frameCache.saveid, NULL, save_x4data, &g_frameCache);
    if(sv_err!=0)
    {
        printf("Create thread failed!\n");
        return sv_err;
    }
    else
    {
        printf("save x4data thread start:%d\n",g_frameCache.saveid);
    }

    int bt_err = pthread_create(&g_frameCache.btid, NULL, bt_recv_thread, &g_frameCache);
    if(bt_err!=0)
    {
        printf("Create thread failed!\n");
        return bt_err;
    }
    else
    {
        printf("Bluetooth listen thread start:%d\n",g_frameCache.btid);
    }
    x4driver_start_sweep(x4driver);
    for (;;)
    {
        //usleep(100);
        if(g_readDone == 1)
	{
                switchAnt(g_antNum);
 		usleep(200);
		
 		x4driver_start_sweep(x4driver);
	}
    }

}
