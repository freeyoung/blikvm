/*******************************************************************************
 *                            CHANGE HISTORY                                   *
 *-----------------------------------------------------------------------------*
 *   <Date>   | <Version> | <Author>      |            <Description>           *
 *-----------------------------------------------------------------------------*
 * 2022-12-29 | 0.1       | Thomasvon     |                 create
 ******************************************************************************/
#include <sys/select.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "blikvm_switch.h"
#include "common/blikvm_uart/blikvm_uart.h"
#include "common/blikvm_log/blikvm_log.h"
#include "common/blikvm_socket/blikvm_socket.h"

#define TAG "SWITCH"
#define SERIAL_TIME_OUT_MS  (2050)
static blikvm_int32_t g_serial_fd = 0;
static blikvm_interactive_t g_switch;
static blikvm_domainsocker_rev_t g_rev_buff = {0};

static blikvm_void_t *blikvm_switch_state_loop(void *_);
static blikvm_void_t *blikvm_switch_control_loop(void *_);

blikvm_int8_t blikvm_switch_init()
{
    blikvm_int8_t ret = -1;
    do
    {
        g_serial_fd = open_serial_dev("/dev/ttyUSB0",g_serial_fd);
        if( g_serial_fd < 0)
        {
            BLILOG_E(TAG,"init switch failed\n");
            break;
        }

        if(access("/dev/shm/blikvm/",R_OK) != 0)
        {
            BLILOG_E(TAG,"not exit /dev/shm/blikvm/ will creat this dir\n");
            system("mkdir /dev/shm/blikvm/");
            BLILOG_I(TAG,"creat /dev/shm/blikvm/ ok\n");
        }
        system("chmod 777 /dev/shm/blikvm/");
        g_switch.fp = fopen("/dev/shm/blikvm/switch","wb+");
        system("chmod 777 /dev/shm/blikvm/switch");
        if(NULL == g_switch.fp)
        {
            BLILOG_E(TAG,"open /dev/shm/blikvm/switch error\n");
            break;
        }
        else
        {
            blikvm_uint8_t state[] = {0b00000000};
            fwrite(state, sizeof(state) , 1, g_switch.fp );
            fflush(g_switch.fp);
            fclose(g_switch.fp);
            BLILOG_D(TAG,"open /dev/shm/blikvm/switch ok\n");
        }

        g_switch.socket = socket(AF_UNIX, SOCK_DGRAM, 0);
        if(g_switch.socket <= 0)
        {
            BLILOG_E(TAG,"make socket error\n");
            break;
        }
        g_switch.socket_addr.recv_addr.sun_family = AF_UNIX;
        strcpy(g_switch.socket_addr.recv_addr.sun_path, "/var/blikvm/switch.sock");
        
        blikvm_int32_t unlink_ret = unlink("/var/blikvm/switch.sock");
        if (unlink_ret && ENOENT != errno)
        {
            BLILOG_E(TAG,"unlink failed\n");
            break;
        }

        blikvm_int32_t bind_ret = bind(g_switch.socket, (struct sockaddr *)&(g_switch.socket_addr.recv_addr), sizeof(g_switch.socket_addr.recv_addr));
        if (bind_ret < 0)
        {
            BLILOG_E(TAG,"bind failed\n");
            break;
        }
        g_switch.socket_addr.send_addr_len = sizeof(g_switch.socket_addr.send_addr);

        g_switch.init = 1;
    }while(0>1);


    return ret;
}


blikvm_int8_t blikvm_switch_start()
{
    blikvm_int8_t ret = -1;
    do
    {
        if(g_switch.init == 0)
        {
            BLILOG_E(TAG,"switch moudle status is not ok\n");
            break;
        }
        pthread_t blikvm_switch_thread;
        blikvm_int8_t thread_ret = pthread_create(&blikvm_switch_thread, NULL, blikvm_switch_state_loop, NULL);
        if(thread_ret != 0)
        {
            BLILOG_E(TAG,"creat switch state loop thread failed\n");
            break;
        }
        pthread_t blikvm_switch_control_thread;
        thread_ret = pthread_create(&blikvm_switch_control_thread, NULL, blikvm_switch_control_loop, NULL);
        if(thread_ret != 0)
        {
            BLILOG_E(TAG,"creat switch control loop thread failed\n");
            break;
        }
        
    }while(0>1);
    return ret;
}

static blikvm_void_t *blikvm_switch_state_loop(void *_)
{
    do
    {
        fd_set         input;
        blikvm_int32_t  max_fd;
        blikvm_int8_t   data[8];
        struct timeval tv;
        blikvm_int32_t  n;
        while(1)
        {
            /* Initialize the input set */
            FD_ZERO(&input);
            FD_SET(g_serial_fd, &input);

            max_fd = g_serial_fd + 1;

            tv.tv_sec  = SERIAL_TIME_OUT_MS / 1000;
            tv.tv_usec = (SERIAL_TIME_OUT_MS % 1000) * 1000;

            /* Do the select */
            n = select(max_fd, &input, NULL, NULL, &tv);
                        /* See if there was an error */
            if (n < 0) {
                BLILOG_E(TAG,"select failed\n");
                continue;
            }
            else if (n == 0) {
                BLILOG_E(TAG,"serial read timeout in :%d (ms)\n",SERIAL_TIME_OUT_MS);
            }
            else {
                /* We have input */
                if (FD_ISSET(g_serial_fd, &input)) {
                    blikvm_int32_t len = read(g_serial_fd, data, sizeof(data));
                    if (len > 0) {
                        g_switch.fp = fopen("/dev/shm/blikvm/switch","wb+");
                        BLILOG_D(TAG,"switch data:%s, %d\n",data, len);
                        
                        blikvm_uint8_t state[1];
                        switch(data[2])
                        {
                            case '1': 
                                state[0] = 0b00000001;    
                            break;
                            case '2': 
                                state[0] = 0b00000010; 
                            break;
                            case '3': 
                                state[0] = 0b00000011; 
                            break;
                            case '4': 
                                state[0] = 0b00000100; 
                            break;
                            default:
                                BLILOG_E(TAG,"switch get error open value:%d\n",data[2]);
                            break;
                        }

                        blikvm_int32_t ret_len = fwrite(state, sizeof(state) , 1, g_switch.fp);
                        if(ret_len > 0)
                        {
                            BLILOG_D(TAG,"write ok: %d  state:%d\n",ret_len,state[0]);
                        }
                        fflush(g_switch.fp);
                        fclose(g_switch.fp);
                    }
                }
            }
        }
    }while(0>1);
}

static blikvm_void_t *blikvm_switch_control_loop(void *_)
{
    do
    {
        if(g_switch.init != 1U)
        {
            BLILOG_E(TAG,"not init\n");
            break;
        }
        while(1)
        {
            blikvm_int32_t ret = recvfrom(g_switch.socket,(void *)g_rev_buff.recvBuf,DEFAULT_BUF_LEN,
            0,(struct sockaddr *)&(g_switch.socket_addr.send_addr), &(g_switch.socket_addr.send_addr_len));
            if( ret == 1U)
            {
                blikvm_int8_t cont_data[12];
                switch(g_rev_buff.recvBuf[0])
                {
                    
                    case 1:
                        memcpy(cont_data,"SW1\r\nG01gA",strlen("SW1\r\nG01gA")); 
                    break;
                    case 2:
                        memcpy(cont_data,"SW1\r\nG02gA",strlen("SW1\r\nG01gA")); 
                    break;
                    case 3:
                        memcpy(cont_data,"SW1\r\nG03gA",strlen("SW1\r\nG01gA")); 
                    break;
                    case 4:
                        memcpy(cont_data,"SW1\r\nG04gA",strlen("SW1\r\nG01gA")); 
                    break;
                    default:
                        BLILOG_E(TAG,"switch get error command:%d\n",g_rev_buff.recvBuf[0]);
                    break;
                }
                write(g_serial_fd, cont_data, sizeof(cont_data));
            }
            else
            {
                BLILOG_E(TAG,"recv error len:%d\n",ret);
            }
        }
    }while(0>1);
    return NULL;
}