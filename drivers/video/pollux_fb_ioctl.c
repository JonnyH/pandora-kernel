/*
 * linux/drivers/video/pollux_fb_ioctl.c
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/div64.h>

#include <asm/hardware.h>
#include <asm/arch/sys/sys.h>
#include <asm/arch/aesop-pollux.h> 

#include "pollux_fb_cfg.h"
#include "pollux_mlc.h"
#include "pollux_dpc.h"    /* HYUN_DISP */
#include "pollux_fb.h"
#include "pollux_idct.h"
#include "pollux_pwm.h"

#ifndef CONFIG_ARCH_ADD_GPH_F300
#include "cmel_c0283qglc.h"
#else
#include "lb035q02.h"
#endif

#ifdef  GDEBUG
#    define dprintk(fmt, x... )  printk( "%s: " fmt, __FUNCTION__ , ## x)
#else
#    define dprintk( x... )
#endif


#define IS_MLC_P	0
#define IS_MLC_S	1

static u32 gammaIndex =0;
static u32 lightIndex =0;
static u32 sec_h = 480;

static u8 tv_status = COMMAND_RETURN_LCD;

#define COMMAND_LCD_INIT    0x55

int change_light=0;

/* Function declarations */
static int VideoUpdate(struct fb_info *fbi, LPFB_VMEMINFO vmem, u32 dev);
static int SetupVideo(struct fb_info *info, 
				int width, 
				int height, 
				int left, 
				int top, 
				int right, 
				int bottom, 
				u32 tpcol, 
				u32 fourcc, 
				u32 flags, 
				u32 position);
static int VideoOn(struct fb_info *info, u32 dev);
static int VideoOff(struct fb_info *info, u32 dev);
static int CheckVideoOn(struct fb_info *info, u32 viddev);
static int VideoMove (struct fb_info *info, int sx, int sy, int ex, int ey, u32 dev);
static int VideoScale(struct fb_info *info, int srcw, int srch, int dstw, int dsth, u32 dev);
static int SetLayerPriority(u32 priority, u32 dev);
static int SetLuminanceEnhance(int bright, int contrast, u32 dev);
static int SetChrominanceEnhance(int cba, int cbb, int cra, int crb, u32 dev);
static int SetLayerTPColor(u32 layer, u32 color, u32 on, u32 dev);
static int SetLayerInvColor(u32 layer, u32 color, u32 on, u32 dev);
static int SetLayerAlphaBlend(u32 layer, u32 degree, u32 on, u32 dev);
static int SetDispDeviceEnable(int layer, u32 on, u32 dev);
static int SetupRgbLayer(int layer, u32 base, int left, int top, int right, int bottom,
                              u32 hStride, u32 vStride, int en3d, u32 backcol, u32 dev);
static int SetRgbPower(int layer, u32 on, u32 dev);
static int GetDirtflag(int layer, u32 dev);
static int RunIdct(U16 *InputData, U16 *QuantMatrix, U16 * OutputData );
static int TvConfiguration(u8 command, u8 mode, u32 width, u32 Height);
static int LcdChangeSet(u32 cmd, u32 value); 
static u16 R8G8B8toR5G6B5(u32 rgb);
static u32 R5G6B5toR8G8B8(u16 rgb);


static __inline__ void SwitchVideo(int *left, int *right);
static int LcdChangeSet(u32 cmd, u32 value);
static int LcdRightWrite(u32 value);
static void CountDelay(int count);

void InitializeDPC(u8 command, u32 x, u32 y, u32 sec_x, u32 sec_y, u8 mode)
{
    //mode = MES_DPC_VBS_PAL_N;
    
    if( (command == COMMAND_INDIVIDUALLY) || (command == COMMAND_COMMONVIEW)
	            || (command == COMMAND_ONLY_TV) )
    {
        printk("tv dpc seting \n");
        MES_DPC_SelectModule( 1 );
	    MES_DPC_SetClockPClkMode(MES_PCLKMODE_DYNAMIC);
	
		MES_DPC_SetClockOutEnb( 0, CFALSE );
		MES_DPC_SetDPCEnable( CFALSE );
		MES_DPC_SetClockDivisorEnable(CFALSE);
        
#if 1        
        if (command == COMMAND_INDIVIDUALLY) {
            MES_DPC_SetHorizontalUpScaler( CFALSE, 2, 2 );
            
        }
        else MES_DPC_SetHorizontalUpScaler( CTRUE, x, sec_x);
#else
        MES_DPC_SetHorizontalUpScaler( CTRUE, x, sec_x);
#endif        
        //MES_DPC_SetHorizontalUpScaler( CTRUE, 320, );
        
        //----------------------------------------------------------------------
	    // Internal/External Encoder Mode
	    //----------------------------------------------------------------------
    	// VCLK2 : CLKGEN0
    	MES_DPC_SetClockSource  (0, DISPLAY_DPC_SEC_VCLK_SOURCE);	// CLKSRCSEL
    	MES_DPC_SetClockDivisor (0, DISPLAY_DPC_SEC_VCLK_DIV);		// CLKDIV
    	MES_DPC_SetClockOutDelay(0, 0); 							// OUTCLKDELAY
    
    	// VCLK : CLKGEN1
    	MES_DPC_SetClockSource  (1, DISPLAY_DPC_SEC_VCLK2_SOURCE);	// CLKSRCSEL  : CLKGEN0's out
    	MES_DPC_SetClockDivisor (1, DISPLAY_DPC_SEC_VCLK2_DIV);		// CLKDIV
    	MES_DPC_SetClockOutDelay(1, 0); 							// OUTCLKDELAY

    	MES_DPC_SetMode( (MES_DPC_FORMAT)DISPLAY_DPC_SEC_OUTPUT_FORMAT,	// FORMAT
    	                 DISPLAY_DPC_SEC_SCAN_INTERLACE,			// SCANMODE
    	             	 DISPLAY_DPC_SEC_POLFIELD_INVERT,			// POLFIELD
    	             	 CFALSE, 									// RGBMODE
    	             	 CFALSE,       								// SWAPRB
    	             	 (MES_DPC_YCORDER)DISPLAY_DPC_SEC_OUTORDER,	// YCORDER
    	             	 CTRUE,										// YCCLIP
    	             	 CFALSE,  									// Embedded sync
    	             	 (MES_DPC_PADCLK)DISPLAY_DPC_SEC_PAD_VCLK,	// PADCLKSEL
    	             	 DISPLAY_DPC_SEC_CLOCK_INVERT				// PADCLKINV
    				   );
        
        if( (mode==MES_DPC_VBS_NTSC_M) || (mode==MES_DPC_VBS_NTSC_443) ||
                (mode==MES_DPC_VBS_PAL_M) || (mode==MES_DPC_VBS_PSEUDO_PAL) )
        {                    
    	    
            if (command == COMMAND_INDIVIDUALLY){
              	    
              	    MES_DPC_SetHSync( 320,
    					    83,
    					    74,
    					    131,
    					    DISPLAY_DPC_SEC_HSYNC_ACTIVEHIGH );

    	            MES_DPC_SetVSync( 240/2,
    					    32,
    					    32,
    					    49,
    					    DISPLAY_DPC_SEC_VSYNC_ACTIVEHIGH,
    					    240/2,
    					    33,
    					    34,
    					    46 );
            }else{
                MES_DPC_SetHSync( sec_x,
    					    13,
    					    5,
    					    120,
    					    DISPLAY_DPC_SEC_HSYNC_ACTIVEHIGH );
                MES_DPC_SetVSync( sec_y/2,
    					    2,//2,  //DISPLAY_DPC_SEC_VSYNC_SWIDTH,
    					    2,//2,  //DISPLAY_DPC_SEC_VSYNC_FRONT_PORCH,
    					    9,//19, //DISPLAY_DPC_SEC_VSYNC_BACK_PORCH,18
    					    DISPLAY_DPC_SEC_VSYNC_ACTIVEHIGH,
    					     sec_y/2 ,
    					    3,
    					    4,//2,
    					    15 );
                }
            }                
                
        else if( (mode==MES_DPC_VBS_NTSC_N) || (mode==MES_DPC_VBS_PAL_BGHI) ||
                    (mode==MES_DPC_VBS_PAL_N) || (mode==MES_DPC_VBS_PSEUDO_NTSC) )
                {

#if 0
            MES_DPC_SetHSync( TV_X_PAL,
    					    DISPLAY_DPC_SEC_HSYNC_SWIDTH1,
    					    DISPLAY_DPC_SEC_HSYNC_FRONT_PORCH1,
    					    DISPLAY_DPC_SEC_HSYNC_BACK_PORCH1,
    					    DISPLAY_DPC_SEC_HSYNC_ACTIVEHIGH );


    	    MES_DPC_SetVSync( DISPLAY_SEC_MAX_Y_RESOLUTION/2,
    					    DISPLAY_DPC_SEC_VSYNC_SWIDTH1,
    					    DISPLAY_DPC_SEC_VSYNC_FRONT_PORCH1,
    					    DISPLAY_DPC_SEC_VSYNC_BACK_PORCH1,
    					    DISPLAY_DPC_SEC_VSYNC_ACTIVEHIGH,
    					    DISPLAY_SEC_MAX_Y_RESOLUTION/2,
    					    2,
    					    3,
    					    12 );
#else

            
            
            MES_DPC_SetHSync( sec_x,
    					    62,//42
    					    2,
    					    100,
    					    DISPLAY_DPC_SEC_HSYNC_ACTIVEHIGH );
            
            MES_DPC_SetVSync( sec_y/2,
    					    2,    //2
    					    3,  //21
    					    30,   //3
    					    DISPLAY_DPC_SEC_VSYNC_ACTIVEHIGH,
    					    sec_y/2,
    					    2,
    					    3,
    					    30 );


#endif     
     
        }
        
    	MES_DPC_SetVSyncOffset( 0, 0, 0, 0 );
        
        
		switch(DISPLAY_DPC_SEC_OUTPUT_FORMAT)
		{
			case DPC_FORMAT_RGB555:
			case DPC_FORMAT_MRGB555A:
			case DPC_FORMAT_MRGB555B:
				MES_DPC_SetDither(MES_DPC_DITHER_5BIT,		// DPCCTRL1: RDITHER-6bit
							      MES_DPC_DITHER_5BIT,		// DPCCTRL1: GDITHER-6bit
							  	  MES_DPC_DITHER_5BIT); 	// DPCCTRL1: BDITHER-6bit
				break;
			case DPC_FORMAT_RGB565:
			case DPC_FORMAT_MRGB565:
				MES_DPC_SetDither(MES_DPC_DITHER_5BIT,		// DPCCTRL1: RDITHER-6bit
							      MES_DPC_DITHER_6BIT,		// DPCCTRL1: GDITHER-6bit
							      MES_DPC_DITHER_5BIT);		// DPCCTRL1: BDITHER-6bit
				break;
			case DPC_FORMAT_RGB666:
			case DPC_FORMAT_MRGB666:
				MES_DPC_SetDither(MES_DPC_DITHER_6BIT,		// DPCCTRL1: RDITHER-6bit
							      MES_DPC_DITHER_6BIT,		// DPCCTRL1: GDITHER-6bit
							      MES_DPC_DITHER_6BIT);		// DPCCTRL1: BDITHER-6bit
				break;
			default:
				MES_DPC_SetDither(MES_DPC_DITHER_BYPASS,	// DPCCTRL1: RDITHER-6bit
							      MES_DPC_DITHER_BYPASS,	// DPCCTRL1: GDITHER-6bit
							  	  MES_DPC_DITHER_BYPASS);	// DPCCTRL1: BDITHER-6bit
				break;
		}

		MES_DPC_SetDelay( 0,							// DELAYRGB_PVD
	 	             	  DISPLAY_DPC_SEC_SYNC_DELAY,	// DELAYHS_CP1
		             	  DISPLAY_DPC_SEC_SYNC_DELAY,	// DELAYVS_FRAM
		             	  DISPLAY_DPC_SEC_SYNC_DELAY );	// DELAYDE_CP2

		MES_DPC_SetENCEnable( CTRUE );
		udelay( 100 );
		MES_DPC_SetClockDivisorEnable(CTRUE);
		udelay( 100 );
		MES_DPC_SetENCEnable( CFALSE );
		udelay( 100 );
		MES_DPC_SetClockDivisorEnable(CFALSE);
		udelay( 100 );
		MES_DPC_SetENCEnable( CTRUE );
		MES_DPC_SetVideoEncoderPowerDown( CTRUE );
		MES_DPC_SetVideoEncoderMode( (MES_DPC_VBS)mode, CTRUE );
		MES_DPC_SetVideoEncoderFSCAdjust( 0 );
		MES_DPC_SetVideoEncoderBandwidth( MES_DPC_BANDWIDTH_LOW, MES_DPC_BANDWIDTH_LOW);
		MES_DPC_SetVideoEncoderColorControl( 0, 0, 0, 0, 0 );
		
		if( (mode==MES_DPC_VBS_NTSC_M) || (mode==MES_DPC_VBS_NTSC_443) ||
                (mode==MES_DPC_VBS_PAL_M) || (mode==MES_DPC_VBS_PSEUDO_PAL) )
		    MES_DPC_SetVideoEncoderTiming( 64, 1716, 0, 3  );
		else if( (mode==MES_DPC_VBS_NTSC_N) || (mode==MES_DPC_VBS_PAL_BGHI) ||
                (mode==MES_DPC_VBS_PAL_N) || (mode==MES_DPC_VBS_PSEUDO_NTSC) )
		    MES_DPC_SetVideoEncoderTiming( 114/*84*/, 1728, 0, 4 );
		    
		MES_DPC_SetVideoEncoderPowerDown( CFALSE );
		
		MES_DPC_SetDPCEnable( CTRUE );
		MES_DPC_SetClockDivisorEnable(CTRUE);	// CLKENB : Provides internal operating clock.
		MES_DPC_SetClockOutEnb( 0, CFALSE );	// OUTCLKENB : Enable
    }
    
    if( command == COMMAND_LCD_INIT )
    {
        MES_DPC_SelectModule( 0 );
        MES_DPC_SetClockPClkMode(MES_PCLKMODE_ALWAYS);
        MES_DPC_SetClockOutEnb( 0, CFALSE );
	    MES_DPC_SetDPCEnable( CFALSE );
	    MES_DPC_SetClockDivisorEnable(CFALSE);
	    MES_DPC_SetHorizontalUpScaler( CFALSE, 2, 2 );

		// VCLK2 : CLKGEN0
	    MES_DPC_SetClockSource  (0, DISPLAY_DPC_PRI_VCLK_SOURCE);	// CLKSRCSEL
	    MES_DPC_SetClockDivisor (0, DISPLAY_DPC_PRI_VCLK_DIV);		// CLKDIV
	    MES_DPC_SetClockOutDelay(0, 0); 							// OUTCLKDELAY
	    MES_DPC_SetClockOutEnb( 0, CTRUE );							// OUTCLKENB : Enable
	    	
	    // VCLK : CLKGEN1
	    MES_DPC_SetClockSource  (1, DISPLAY_DPC_PRI_VCLK2_SOURCE);	// CLKSRCSEL  : CLKGEN0's out
	    MES_DPC_SetClockDivisor (1, DISPLAY_DPC_PRI_VCLK2_DIV);		// CLKDIV
	    MES_DPC_SetClockOutDelay(1, 0); 							// OUTCLKDELAY
	
        MES_DPC_SetClockDivisorEnable(CTRUE);	// CLKENB : Provides internal operating clock.

	    //----------------------------------------------------------------------
	    // TFT LCD or Internal/External Encoder Mode
	    //----------------------------------------------------------------------
        MES_DPC_SetMode( (	MES_DPC_FORMAT)DISPLAY_DPC_PRI_OUTPUT_FORMAT,	// FORMAT
	    	            DISPLAY_DPC_PRI_TFT_SCAN_INTERLACE,				// SCANMODE
	    	            DISPLAY_DPC_PRI_TFT_POLFIELD_INVERT,			// POLFIELD
	    	            CTRUE, 											// RGBMODE
	    	            CFALSE,       									// SWAPRB
	    	            (MES_DPC_YCORDER)DISPLAY_DPC_PRI_TFT_OUTORDER,	// YCORDER
	    	            CFALSE,											// YCCLIP
	    	            CFALSE,  										// Embedded sync
	    	            (MES_DPC_PADCLK)DISPLAY_DPC_PRI_PAD_VCLK,		// PADCLKSEL
	    	             DISPLAY_DPC_PRI_CLOCK_INVERT					// PADCLKINV
	    			);
			
	    switch(DISPLAY_DPC_PRI_OUTPUT_FORMAT)
	    {
	        case DPC_FORMAT_RGB555:
		    case DPC_FORMAT_MRGB555A:
		    case DPC_FORMAT_MRGB555B:
		        MES_DPC_SetDither(MES_DPC_DITHER_5BIT,		// DPCCTRL1: RDITHER-6bit
						      MES_DPC_DITHER_5BIT,		// DPCCTRL1: GDITHER-6bit
						      MES_DPC_DITHER_5BIT); 	// DPCCTRL1: BDITHER-6bit
			    break;
	        case DPC_FORMAT_RGB565:
		    case DPC_FORMAT_MRGB565:
			    MES_DPC_SetDither(MES_DPC_DITHER_5BIT,		// DPCCTRL1: RDITHER-6bit
							  MES_DPC_DITHER_6BIT,		// DPCCTRL1: GDITHER-6bit
						      MES_DPC_DITHER_5BIT);		// DPCCTRL1: BDITHER-6bit
		        break;
		    case DPC_FORMAT_RGB666:
		    case DPC_FORMAT_MRGB666:
		        MES_DPC_SetDither(MES_DPC_DITHER_6BIT,		// DPCCTRL1: RDITHER-6bit
						  MES_DPC_DITHER_6BIT,		// DPCCTRL1: GDITHER-6bit
						  MES_DPC_DITHER_6BIT);		// DPCCTRL1: BDITHER-6bit
		        break;
		    default:
		        MES_DPC_SetDither(MES_DPC_DITHER_BYPASS,	// DPCCTRL1: RDITHER-6bit
							  MES_DPC_DITHER_BYPASS,	// DPCCTRL1: GDITHER-6bit
						      MES_DPC_DITHER_BYPASS);	// DPCCTRL1: BDITHER-6bit
			    break;
        }


        MES_DPC_SetHSync( x ,
	    		      DISPLAY_DPC_PRI_TFT_HSYNC_SWIDTH,
	    			  DISPLAY_DPC_PRI_TFT_HSYNC_FRONT_PORCH,
	    			  DISPLAY_DPC_PRI_TFT_HSYNC_BACK_PORCH,
	    		      DISPLAY_DPC_PRI_TFT_HSYNC_ACTIVEHIGH );
	
        MES_DPC_SetVSync( y ,
	    			  DISPLAY_DPC_PRI_TFT_VSYNC_SWIDTH,
	    			  DISPLAY_DPC_PRI_TFT_VSYNC_FRONT_PORCH,
	    			  DISPLAY_DPC_PRI_TFT_VSYNC_BACK_PORCH,
	    			  DISPLAY_DPC_PRI_TFT_VSYNC_ACTIVEHIGH,
	    					  1,
	    					  1,
	    					  1,
	    					  1);

        MES_DPC_SetVSyncOffset( 0, 0, 0, 0 );
        MES_DPC_SetDelay( 0,							// DELAYRGB_PVD
	 	              DISPLAY_DPC_PRI_SYNC_DELAY,	// DELAYHS_CP1
		              DISPLAY_DPC_PRI_SYNC_DELAY,	// DELAYVS_FRAM
		              DISPLAY_DPC_PRI_SYNC_DELAY );	// DELAYDE_CP2

        MES_DPC_SetSecondaryDPCSync( CFALSE );
        MES_DPC_SetDPCEnable( CTRUE );
    }        
}

void InitializeMLC(u8 command, u32 x, u32 y, u32 sec_x, u32 sec_y)
{
	
    if( (command == COMMAND_INDIVIDUALLY) || (command == COMMAND_COMMONVIEW)
	            || (command == COMMAND_ONLY_TV) )
    {
        U32 X_STRIDE = DISPLAY_MLC_BYTE_PER_PIXEL * x;
        U32 SEC_MLC_FRAME_BASE;
		U32 SEC_X_RESOLUTION = x;
		U32 SEC_Y_RESOLUTION = y;
        
        printk("tv mlc seting \n");
        
        if( ( command == COMMAND_COMMONVIEW ) || (command == COMMAND_ONLY_TV) ){   
            SEC_MLC_FRAME_BASE = POLLUX_FB_PIO_BASE;
        }
        else if(command == COMMAND_INDIVIDUALLY) {   
#if 0           
            SEC_MLC_FRAME_BASE = POLLUX_FB_PIO_BASE + ( x * DISPLAY_MLC_BYTE_PER_PIXEL );
            X_STRIDE = DISPLAY_MLC_BYTE_PER_PIXEL * ( x + SEC_X_RESOLUTION );
#else
            SEC_MLC_FRAME_BASE = POLLUX_FB_PIO_BASE;
#endif        
        }
        
        MES_MLC_SetClockPClkMode(1, MES_PCLKMODE_DYNAMIC );
		MES_MLC_SetClockBClkMode(1, MES_BCLKMODE_DYNAMIC );
        
        MES_MLC_SetLayerPriority(1, (MES_MLC_PRIORITY)DISPLAY_MLC_VIDEO_LAYER_PRIORITY );
	    MES_MLC_SetTopPowerMode (1, CTRUE );
	    MES_MLC_SetTopSleepMode (1, CFALSE );
        
        if(command == COMMAND_INDIVIDUALLY) MES_MLC_SetFieldEnable(1, CTRUE );
        else MES_MLC_SetFieldEnable(1, CFALSE );
        
        MES_MLC_SetScreenSize( 1, SEC_X_RESOLUTION, SEC_Y_RESOLUTION );		
		MES_MLC_SetBackground(1, 0xFF0000);
	
		MES_MLC_SetMLCEnable( 1, CTRUE );
		MES_MLC_SetTopDirtyFlag(1);
		
		//----------------------------------------------------------------------
		// SECONDARY RGB Layer SCREEN Field
		//----------------------------------------------------------------------
		MES_MLC_SetAlphaBlending  ( 1, LAYER_DISPLAY_SCREEN_RGB, CFALSE, 15 );
		MES_MLC_SetTransparency   ( 1, LAYER_DISPLAY_SCREEN_RGB, CFALSE,  0 );
		MES_MLC_SetColorInversion ( 1, LAYER_DISPLAY_SCREEN_RGB, CFALSE,  0 );
		MES_MLC_SetLockSize       ( 1, LAYER_DISPLAY_SCREEN_RGB, 4 );
		MES_MLC_Set3DEnb          ( 1, LAYER_DISPLAY_SCREEN_RGB, CFALSE );
	
	    MES_MLC_SetLayerPowerMode ( 1, LAYER_DISPLAY_SCREEN_RGB, CTRUE );
		MES_MLC_SetLayerSleepMode ( 1, LAYER_DISPLAY_SCREEN_RGB, CFALSE );
		
		MES_MLC_SetFormat         ( 1, LAYER_DISPLAY_SCREEN_RGB, (MES_MLC_RGBFMT)DISPLAY_MLC_RGB_FORMAT );
		MES_MLC_SetPosition       ( 1, LAYER_DISPLAY_SCREEN_RGB, 0, 0, SEC_X_RESOLUTION-1, SEC_Y_RESOLUTION-1 );

		MES_MLC_SetRGBLayerStride ( 1, LAYER_DISPLAY_SCREEN_RGB, DISPLAY_MLC_BYTE_PER_PIXEL, X_STRIDE );
		MES_MLC_SetRGBLayerAddress( 1, LAYER_DISPLAY_SCREEN_RGB, SEC_MLC_FRAME_BASE );
		
		MES_MLC_SetRGBLayerInvalidPosition( 1, LAYER_DISPLAY_SCREEN_RGB, 0, 0, 0, 0, 0, CFALSE );
		MES_MLC_SetRGBLayerInvalidPosition( 1, LAYER_DISPLAY_SCREEN_RGB, 1, 0, 0, 0, 0, CFALSE );
		
		MES_MLC_SetLayerEnable( 1, LAYER_DISPLAY_SCREEN_RGB, CTRUE );
		MES_MLC_SetDirtyFlag( 1, LAYER_DISPLAY_SCREEN_RGB );
		
    }    
        
    if( command == COMMAND_LCD_INIT )
    {    
        /* disable function mlc1(tv)  */
        
        MES_MLC_SetClockPClkMode( 0, MES_PCLKMODE_ALWAYS );
        MES_MLC_SetClockBClkMode( 0, MES_BCLKMODE_ALWAYS );    	            
	    //----------------------------------------------------------------------
	    // PRIMARY RGB Layer TOP Field
	    //----------------------------------------------------------------------
	    MES_MLC_SetTop3DAddrChangeSync( 0, MES_MLC_3DSYNC_PRIMARY );  
	    MES_MLC_SetLayerPriority( 0, (MES_MLC_PRIORITY)DISPLAY_MLC_VIDEO_LAYER_PRIORITY );
	    MES_MLC_SetTopPowerMode ( 0, CTRUE );
	    MES_MLC_SetTopSleepMode ( 0, CFALSE );
	
        MES_MLC_SetFieldEnable( 0, CFALSE );
        MES_MLC_SetScreenSize( 0, x, y);
	    MES_MLC_SetBackground(0, 0x00000000);
	    MES_MLC_SetMLCEnable( 0, CTRUE );
	    MES_MLC_SetTopDirtyFlag(0);
		
	    //----------------------------------------------------------------------
	    // PRIMARY RGB Layer SCREEN Field
	    //----------------------------------------------------------------------
	    MES_MLC_SetAlphaBlending  ( 0, LAYER_DISPLAY_SCREEN_RGB, CFALSE,  0 );  //15
	    MES_MLC_SetTransparency   ( 0, LAYER_DISPLAY_SCREEN_RGB, CFALSE,  0 );
	    MES_MLC_SetColorInversion ( 0, LAYER_DISPLAY_SCREEN_RGB, CFALSE,  0 );
	    MES_MLC_SetLockSize       ( 0, LAYER_DISPLAY_SCREEN_RGB, 8 );         //4
	    MES_MLC_Set3DEnb          ( 0, LAYER_DISPLAY_SCREEN_RGB, CFALSE );
	
	    MES_MLC_SetLayerPowerMode ( 0, LAYER_DISPLAY_SCREEN_RGB, CTRUE );
	    MES_MLC_SetLayerSleepMode ( 0, LAYER_DISPLAY_SCREEN_RGB, CFALSE );
		
	    MES_MLC_SetFormat         ( 0, LAYER_DISPLAY_SCREEN_RGB, (MES_MLC_RGBFMT)DISPLAY_MLC_RGB_FORMAT );
	    MES_MLC_SetPosition       ( 0, LAYER_DISPLAY_SCREEN_RGB, 0, 0, x-1, y-1 );
	    MES_MLC_SetRGBLayerStride ( 0, LAYER_DISPLAY_SCREEN_RGB, DISPLAY_MLC_BYTE_PER_PIXEL, (DISPLAY_MLC_BYTE_PER_PIXEL * x) );
        //	MES_MLC_SetRGBLayerAddress( 0, LAYER_DISPLAY_SCREEN_RGB, PRI_MLC_FRAME_BASE );
		
	    MES_MLC_SetRGBLayerInvalidPosition( 0,LAYER_DISPLAY_SCREEN_RGB, 0, 0, 0, 0, 0, CFALSE );
	    MES_MLC_SetRGBLayerInvalidPosition( 0, LAYER_DISPLAY_SCREEN_RGB, 1, 0, 0, 0, 0, CFALSE );
		
	    MES_MLC_SetLayerEnable( 0, LAYER_DISPLAY_SCREEN_RGB, CTRUE );
	    MES_MLC_SetDirtyFlag( 0, LAYER_DISPLAY_SCREEN_RGB );
    }
}

void InitializePWM(void)
{
    MES_PWM_SetClockSource (0, SYSTEM_CLOCK_PWM_SELPLL);
	MES_PWM_SetClockDivisor(0, SYSTEM_CLOCK_PWM_DIV);		                            
	MES_PWM_SetClockPClkMode(MES_PCLKMODE_ALWAYS);
	MES_PWM_SetClockDivisorEnable(CTRUE);		                        
	MES_PWM_SetPreScale(PWM_DISPLAY_LCD_PRI_BRIGHTNESS ,99);			
	MES_PWM_SetPolarity (PWM_DISPLAY_LCD_PRI_BRIGHTNESS,
							(DISPLAY_LCD_PRI_BIRGHTNESS_ACTIVE) ?
							(enum POL) POL_BYPASS : (enum POL) POL_INVERTED );
	MES_PWM_SetPeriod   (PWM_DISPLAY_LCD_PRI_BRIGHTNESS, 4);	        
	MES_PWM_SetDutyCycle(PWM_DISPLAY_LCD_PRI_BRIGHTNESS, 2);            
}



#define GPIO_LCD_AVDD			POLLUX_GPB14
#define GPIO_LCD_ENB			POLLUX_GPB15

#ifndef CONFIG_ARCH_ADD_GPH_F300
void	
InitializeLCD(int flag)
{
	LCD_CS_HIGH;
	LCD_SCL_HIGH;
	LCD_SDI_HIGH;
	
    pollux_gpio_setpin(GPIO_LCD_AVDD, CFALSE);
	pollux_gpio_setpin(GPIO_LCD_ENB, CFALSE);
	CountDelay(10);
	pollux_gpio_setpin(GPIO_LCD_ENB, CTRUE);

	lcd_SetW( 0x02, 0x0182 );	udelay( 100 );  //18bit
	
    if(flag){
        /* 320 x 240 */        
        lcd_SetW( 0x03, 0x8111 ); 	//SS=1 AM=01 ID[1:]=01
        lcd_SetW( 0x20, 0x0000 ); 
		lcd_SetW( 0x21, 0x013f ); 
    }else{
        /* 240 x 320 */
        lcd_SetW( 0x03, 0x8130 );  
        lcd_SetW( 0x20, 0x0000 ); 
        lcd_SetW( 0x21, 0x0000 ); 
    }

    lcd_SetW( 0x10, 0x0 );
	lcd_SetW( 0x05, 0x0001 );
//	lcd_SetW( 0x18, 0x28 );	udelay( 100 );	//80hz bnjang	
	lcd_SetW( 0x22, 0xffff );	CountDelay( 1000 );

	pollux_gpio_setpin(GPIO_LCD_AVDD, CTRUE);
}


#if 0 /* HYUN */
static irqreturn_t dpc_vsync_interrupt(int irq, void *dev_id)
{
    if( change_light ){
        LcdRightWrite(lightIndex);
        change_light = 0;
     }else MES_DPC_ClearInterruptPending(0); 

    //MES_DPC_ClearInterruptPending(0);
    
    return irq;
}
#endif


#else  /* lg lcd */


void
InitializeLCD(void)
{
    unsigned int i;
	unsigned char deviceID;
	lcd_seting setLcd;
	
	SCL_HIGH;
	SDA_HIGH;
	CS_HIGH;
	
	pollux_gpio_setpin(GPIO_LCD_ENB, 0);  // lcd reset
    mdelay(1);
    pollux_gpio_setpin(GPIO_LCD_ENB, 1);
    mdelay(1);
	
	setLcd.pVal = valueREG;
	deviceID = DEVICEID_LGPHILIPS;

	for(i=0; i < MAX_REG ; i++){
		setLcd.reg = REG_NO[i];
		lcdSetWrite(deviceID, &setLcd);	
		setLcd.pVal++;
	}
}


#endif   /* CONFIG_ARCH_ADD_GPH_F300 */




int 
pollux_ioctl_init(struct pollux_fb_ioctl_ops *ioctl_ops)
{
	if(ioctl_ops == NULL)
		return -EINVAL;

	ioctl_ops->video_update = VideoUpdate;
	ioctl_ops->setup_video = SetupVideo;
	ioctl_ops->video_on = VideoOn;
	ioctl_ops->video_off = VideoOff;
	ioctl_ops->check_video_on = CheckVideoOn;
	ioctl_ops->video_move = VideoMove;
	ioctl_ops->video_scale = VideoScale;
	ioctl_ops->set_layer_priority = SetLayerPriority;
	ioctl_ops->set_luminance_enhance = SetLuminanceEnhance;
	ioctl_ops->set_chrominance_enhance = SetChrominanceEnhance;
	ioctl_ops->set_layer_tpcolor = SetLayerTPColor;
	ioctl_ops->Set_layer_invcolor = SetLayerInvColor;
	ioctl_ops->set_layer_alpha_blend = SetLayerAlphaBlend;
	ioctl_ops->set_disp_device_enable = SetDispDeviceEnable;
	ioctl_ops->get_dirtflag = GetDirtflag;
	ioctl_ops->run_idct = RunIdct;
	ioctl_ops->setup_rgb_layer = SetupRgbLayer;
	ioctl_ops->set_rgb_power = SetRgbPower;
	ioctl_ops->lcd_change_set = LcdChangeSet;
	ioctl_ops->tv_configuration = TvConfiguration;
	ioctl_ops->R8G8B8_to_R5G6B5 = R8G8B8toR5G6B5;
	ioctl_ops->R5G6B5_to_R8G8B8 = R5G6B5toR8G8B8;
    
    MES_MLC_Initialize();
	MES_MLC_SetBaseAddress(0, POLLUX_VA_MLC_PRI);
	MES_MLC_SetBaseAddress(1, POLLUX_VA_MLC_SEC);

#ifdef CONFIG_ARCH_ADD_GPH_F300   
    MES_PWM_Initialize();
	MES_PWM_SelectModule( 0);
	MES_PWM_SetBaseAddress(POLLUX_VA_PWM);
	MES_PWM_OpenModule();
#endif

    MES_IDCT_Initialize();
	MES_IDCT_SetBaseAddress(POLLUX_VA_IDCT);
    MES_DPC_Initialize();
    MES_DPC_SelectModule( 0 );
    MES_DPC_SetBaseAddress(POLLUX_VA_DPC_PRI);
    MES_DPC_SelectModule( 1 );
    MES_DPC_SetBaseAddress(POLLUX_VA_DPC_SEC);
	
	
    return 0;
}


static int 
VideoUpdate(struct fb_info *info, LPFB_VMEMINFO vmem, u32 dev)
{
	int selMLC = IS_MLC_P;
	int result = 0;
	int video_layer = 2;
	struct pollux_fb_info *fbi;
	
	fbi = info->par;
	
	if(dev == SEC_VIDEO && fbi->disp_info.use_extend_display == CTRUE)
		selMLC = IS_MLC_S;
		
	MES_MLC_SetYUVLayerStride(selMLC, vmem->LuStride, vmem->CbStride, vmem->CrStride);
	MES_MLC_SetYUVLayerAddress(selMLC,vmem->LuOffset, vmem->CbOffset, vmem->CrOffset);
	MES_MLC_SetDirtyFlag(selMLC, video_layer);
	
	if( (tv_status == COMMAND_ONLY_TV) || (tv_status == COMMAND_COMMONVIEW) )
	{
	    MES_MLC_SetYUVLayerStride(IS_MLC_S, vmem->LuStride, vmem->CbStride, vmem->CrStride);
	    MES_MLC_SetYUVLayerAddress(IS_MLC_S,vmem->LuOffset, vmem->CbOffset, vmem->CrOffset);
	    MES_MLC_SetDirtyFlag(IS_MLC_S, video_layer);
	}
	return result;
}


static int 
SetupVideo(struct fb_info *info, 
				int width, 
				int height, 
				int left, 
				int top, 
				int right, 
				int bottom, 
				u32 tpcol, 
				u32 fourcc, 
				u32 flags, 
				u32 position)
{
	int result = 0;
	int selMLC = IS_MLC_P;
	int x_resol;
	int y_resol;
	struct pollux_fb_info *fbi;
	
	fbi = info->par;
	
	dprintk(" %d> SetupVideo(%d)(%d by %d)(l:%d, t:%d, r:%d, b:%d)(col:0x%x)(fourcc:0x%x)(flags:0x%08x)\r\n",
			position, fbi->disp_info.video_layer, width, height, left, top, right, bottom, tpcol, fourcc, flags);

	if(position == SEC_VIDEO  && 1)
		selMLC = IS_MLC_S;
		
    if(selMLC == IS_MLC_P)
		x_resol = DISPLAY_PRI_MAX_X_RESOLUTION, y_resol = DISPLAY_PRI_MAX_Y_RESOLUTION;
	else
		x_resol = DISPLAY_SEC_MAX_X_RESOLUTION, y_resol = DISPLAY_SEC_MAX_Y_RESOLUTION;	

	// calculate video position.
	SwitchVideo(&left, &right);
	
	// set video image position, scale.
	MES_MLC_SetPosition(selMLC, fbi->disp_info.video_layer, left, top, right, bottom);	
#if 0	
	MES_MLC_SetYUVLayerScale(selMLC, width-2, height-2, 
					  !(right-left) ? 2 : (right-left), 
					  !(bottom-top) ? 2 : (bottom-top), 
                        CTRUE, CTRUE);
#else
    MES_MLC_SetYUVLayerScale(selMLC, width, height, 
					  !(right-left) ? 2 : (right-left), 
					  !(bottom-top) ? 2 : (bottom-top), 
                        CTRUE, CTRUE);
#endif
                      
    // set window transparency color.
	MES_MLC_SetTransparency(selMLC, fbi->disp_info.screen_layer, CTRUE, (tpcol & 0x00ffffff));
	MES_MLC_SetDirtyFlag(selMLC, fbi->disp_info.screen_layer);		
	MES_MLC_SetScreenSize(selMLC, x_resol, y_resol);
	MES_MLC_SetBackground(selMLC, (tpcol & 0x00ffffff));
	MES_MLC_SetTopDirtyFlag(selMLC);
	
	// set etc video layer.
	MES_MLC_SetLockSize(selMLC, fbi->disp_info.video_layer, 8);
	MES_MLC_Set3DEnb(selMLC, fbi->disp_info.video_layer, CFALSE);
	MES_MLC_SetAlphaBlending (selMLC, fbi->disp_info.video_layer, CFALSE, 0);	
	MES_MLC_SetColorInversion(selMLC, fbi->disp_info.video_layer, CFALSE, 0);
	MES_MLC_SetDirtyFlag(selMLC, fbi->disp_info.video_layer);

    if( (tv_status == COMMAND_ONLY_TV) || (tv_status == COMMAND_COMMONVIEW) )
    {
        // calculate video position.
	    SwitchVideo(&left, &right);
        
        // set video image position, scale
	    MES_MLC_SetPosition(IS_MLC_S, fbi->disp_info.video_layer, left, top, right, bottom);	
        MES_MLC_SetYUVLayerScale(IS_MLC_S, width, height, 
					  !(right-left) ? 2 : (right-left), 
					  !(bottom-top) ? 2 : (bottom-top), 
                        CTRUE, CTRUE);

        // set window transparency color.
	    MES_MLC_SetTransparency(IS_MLC_S, fbi->disp_info.screen_layer, CTRUE, (tpcol & 0x00ffffff));
	    MES_MLC_SetDirtyFlag(IS_MLC_S, fbi->disp_info.screen_layer);		
	    MES_MLC_SetScreenSize(IS_MLC_S, x_resol, y_resol);
	    MES_MLC_SetBackground(IS_MLC_S, (tpcol & 0x00ffffff));
	    MES_MLC_SetTopDirtyFlag(IS_MLC_S);
	
	    // set etc video layer.
	    MES_MLC_SetLockSize(IS_MLC_S, fbi->disp_info.video_layer, 8);
	    MES_MLC_Set3DEnb(IS_MLC_S, fbi->disp_info.video_layer, CFALSE);
	    MES_MLC_SetAlphaBlending (IS_MLC_S, fbi->disp_info.video_layer, CFALSE, 0);	
	    MES_MLC_SetColorInversion(IS_MLC_S, fbi->disp_info.video_layer, CFALSE, 0);
	    MES_MLC_SetDirtyFlag(IS_MLC_S, fbi->disp_info.video_layer);
    }

    return result;
}



static int 
VideoOn(struct fb_info *info, u32 dev)
{
	int result = 0;
	int selMLC = IS_MLC_P;
	struct pollux_fb_info *fbi;
	
	fbi = info->par;
	
	if(dev == SEC_VIDEO  && fbi->disp_info.use_extend_display == CTRUE)
		selMLC = IS_MLC_S, fbi->disp_info.secondary_video_on = CTRUE;
	else	 
		selMLC = IS_MLC_P, fbi->disp_info.primary_video_on = CTRUE;

	MES_MLC_SetLayerEnable(selMLC, fbi->disp_info.video_layer, CTRUE);
	MES_MLC_SetDirtyFlag(selMLC, fbi->disp_info.video_layer);
    
    if( (tv_status == COMMAND_ONLY_TV) || (tv_status == COMMAND_COMMONVIEW) )
    {
	    MES_MLC_SetLayerEnable(IS_MLC_S, fbi->disp_info.video_layer, CTRUE);
	    MES_MLC_SetDirtyFlag(IS_MLC_S, fbi->disp_info.video_layer);
    }
	return result;
}


static int 
VideoOff(struct fb_info *info, u32 dev)
{
	int result = 0;
	int selMLC = IS_MLC_P;
	struct pollux_fb_info *fbi;
	
	fbi = info->par;

	if(dev == SEC_VIDEO  && fbi->disp_info.use_extend_display == CTRUE)
		selMLC = IS_MLC_S;

	MES_MLC_SetLayerEnable(selMLC, fbi->disp_info.video_layer, CFALSE);
	MES_MLC_SetDirtyFlag(selMLC, fbi->disp_info.video_layer);

    if( (tv_status == COMMAND_ONLY_TV) || (tv_status == COMMAND_COMMONVIEW) )
    {
	    MES_MLC_SetLayerEnable(IS_MLC_S, fbi->disp_info.video_layer, CFALSE);
	    MES_MLC_SetDirtyFlag(IS_MLC_S, fbi->disp_info.video_layer);
    }

	return result;
}


static int 
CheckVideoOn(struct fb_info *info, u32 viddev)
{
	struct pollux_fb_info *fbi;
	
	fbi = info->par;

	if(viddev == PRI_VIDEO)
		return fbi->disp_info.primary_video_on;
	else
		return fbi->disp_info.secondary_video_on;
}


static int 
VideoMove (struct fb_info *info, int sx, int sy, int ex, int ey, u32 dev)
{
	int result = 0;
	int selMLC = IS_MLC_P;
	struct pollux_fb_info *fbi;
	
	fbi = info->par;

	if(dev == SEC_VIDEO && fbi->disp_info.use_extend_display == CTRUE)
		selMLC = IS_MLC_S;

	// calculate video position.
	SwitchVideo(&sx, &ex);
#if 0	
	MES_MLC_SetPosition(selMLC, fbi->disp_info.video_layer, sx, sy, ex-2, ey-2);	
#else
    MES_MLC_SetPosition(selMLC, fbi->disp_info.video_layer, sx, sy, ex, ey);	
#endif	
	
	MES_MLC_SetDirtyFlag(selMLC, fbi->disp_info.video_layer);

    if( (tv_status == COMMAND_ONLY_TV) || (tv_status == COMMAND_COMMONVIEW) )
    {
        MES_MLC_SetPosition(IS_MLC_S, fbi->disp_info.video_layer, sx, sy, ex, ey);	
        MES_MLC_SetDirtyFlag(IS_MLC_S, fbi->disp_info.video_layer);
    }

	return result;
}


static int 
VideoScale(struct fb_info *info, int srcw, int srch, int dstw, int dsth, u32 dev)
{
	int result = 0;
	int selMLC = IS_MLC_P;
	struct pollux_fb_info *fbi;
	
	fbi = info->par;
	
	
	if(dev == SEC_VIDEO && fbi->disp_info.use_extend_display == CTRUE)
		selMLC = IS_MLC_P;

	if(dstw == 0)	dstw = 1;
	if(dsth == 0)	dsth = 1;

#if 0	
	MES_MLC_SetYUVLayerScale(selMLC, srcw-2, srch-2, dstw, dsth, CTRUE, CTRUE);	
#else	
	MES_MLC_SetYUVLayerScale(selMLC, srcw, srch, dstw, dsth, CFALSE, CFALSE);
#endif	
	
	MES_MLC_SetDirtyFlag(selMLC, fbi->disp_info.video_layer);	

    if( (tv_status == COMMAND_ONLY_TV) || (tv_status == COMMAND_COMMONVIEW) )
    {
        MES_MLC_SetYUVLayerScale(IS_MLC_S, srcw, srch, dstw, dsth, CFALSE, CFALSE);
        MES_MLC_SetDirtyFlag(IS_MLC_S, fbi->disp_info.video_layer);
    }        
    
	return result;
}


static int	
SetLayerPriority(u32 priority, u32 dev)
{
	int result = 0;
	int selMLC = IS_MLC_P;
	
	dprintk("SetLayerPriority (priority:%d, mlc:%d)\n", 
								priority, dev);

	if(dev == SEC_MLC) selMLC = IS_MLC_S;	/* Primary or secondary */
	else selMLC = IS_MLC_P;

	switch(priority)
	{
	case 0:
		MES_MLC_SetLayerPriority(selMLC, MES_MLC_PRIORITY_VIDEOFIRST);  /* < video layer > layer0 > layer1 */
		break;
	case 1:
		MES_MLC_SetLayerPriority(selMLC, MES_MLC_PRIORITY_VIDEOSECOND); /* < layer0 > video layer > layer1 */
		break;
	case 2:
		MES_MLC_SetLayerPriority(selMLC, MES_MLC_PRIORITY_VIDEOTHIRD); /* < layer0 > layer1 > video layer */
		break;
	default:
		dprintk(" SetLayerPriority: Not support priority num(0~2), (%d) \n", priority);
		result = -EINVAL;
	}
	
	MES_MLC_SetTopDirtyFlag(selMLC);
	
	if( (tv_status == COMMAND_ONLY_TV) || (tv_status == COMMAND_COMMONVIEW) )
	{
	    switch(priority)
	    {
	        case 0:
		        MES_MLC_SetLayerPriority(IS_MLC_S, MES_MLC_PRIORITY_VIDEOFIRST);  /* < video layer > layer0 > layer1 */
		        break;
	        case 1:
		        MES_MLC_SetLayerPriority(IS_MLC_S, MES_MLC_PRIORITY_VIDEOSECOND); /* < layer0 > video layer > layer1 */
		        break;
	        case 2:
		        MES_MLC_SetLayerPriority(IS_MLC_S, MES_MLC_PRIORITY_VIDEOTHIRD); /* < layer0 > layer1 > video layer */
		        break;
	        default:
		       dprintk(" SetLayerPriority: Not support priority num(0~2), (%d) \n", priority);
		    result = -EINVAL;
	    }
	    MES_MLC_SetTopDirtyFlag(IS_MLC_S);
	}
	
	return result;
}


static int
SetLuminanceEnhance(int bright, int contrast, u32 dev)
{
	int result = 0;
	int selMLC;

	dprintk("Luminance (bright:%d, contrast:%d, mlc:%d)\n", 
								(char)bright, (char)contrast, dev);

	if(dev == PRI_MLC) selMLC = IS_MLC_P;	/* Primary or secondary */
	else if(dev == SEC_MLC) selMLC = IS_MLC_S;
	else return -EINVAL;

	if(contrast > 8)   contrast=7; if(contrast <= 0)    contrast=0;
	if(bright   > 128) bright=127; if(bright   <= -128) bright=-128;

	MES_MLC_SetYUVLayerLumaEnhance(selMLC, (U32)contrast, (S32)bright);

    if( (tv_status == COMMAND_ONLY_TV) || (tv_status == COMMAND_COMMONVIEW) )
    {
        MES_MLC_SetYUVLayerLumaEnhance(IS_MLC_S, (U32)contrast, (S32)bright);
    }
	
	return result;
}


static int	
SetChrominanceEnhance(int cba, int cbb, int cra, int crb, u32 dev)
{
	int result = 0;
	int selMLC;
	int i;

	dprintk("Chrominance (cba:%d, cbb:%d, cra:%d, crb:%d, mlc:%d)\n", 
								cba, cbb, cra, crb, dev);

	if(dev == PRI_MLC) selMLC = IS_MLC_P;	/* Primary or secondary */
	else if(dev == SEC_MLC) selMLC = IS_MLC_S;
	else return -EINVAL;

	if(cba > 128) cba=127; if(cba <= -128) cba=-128;
	if(cbb > 128) cbb=127; if(cbb <= -128) cbb=-128;
	if(cra > 128) cra=127; if(cra <= -128) cra=-128;
	if(crb > 128) crb=127; if(crb <= -128) crb=-128;		

	for(i=1; i<5; i++)	
		MES_MLC_SetYUVLayerChromaEnhance(selMLC, i, cba, cbb, cra, crb);
    
    if( (tv_status == COMMAND_ONLY_TV) || (tv_status == COMMAND_COMMONVIEW) )
    {
        for(i=1; i<5; i++)	
		    MES_MLC_SetYUVLayerChromaEnhance(IS_MLC_S, i, cba, cbb, cra, crb);
    }
	return result;
}


static int 
SetLayerTPColor(u32 layer, u32 color, u32 on, u32 dev)
{
	int result = 0;
	int selMLC;
	
	dprintk("SetLayerTPColor (layer:%d color:0x%08x, on:%d, mlc:%d)\n", 
								layer, color, on, dev);

	if(dev == PRI_MLC) selMLC = IS_MLC_P;	/* Primary or secondary */
	else if(dev == SEC_MLC) selMLC = IS_MLC_S;
	else return -EINVAL;

	if(on) 	MES_MLC_SetTransparency(selMLC, layer, CTRUE, (color & 0x00ffffff));
	else 	MES_MLC_SetTransparency(selMLC, layer, CFALSE, 0);

	MES_MLC_SetDirtyFlag(selMLC, layer);
	
	if( (tv_status == COMMAND_ONLY_TV) || (tv_status == COMMAND_COMMONVIEW) ){
	    if(on) 	MES_MLC_SetTransparency(IS_MLC_S, layer, CTRUE, (color & 0x00ffffff));
	    else 	MES_MLC_SetTransparency(IS_MLC_S, layer, CFALSE, 0);

	    MES_MLC_SetDirtyFlag(IS_MLC_S, layer);        
	}
	return result;
}


static int
SetLayerInvColor(u32 layer, u32 color, u32 on, u32 dev)
{
	int result = 0;
	int selMLC;

	dprintk("SetLayerInvColor (layer:%d color:0x%08x, on:%d, mlc:%d)\n", 
								layer, color, on, dev);

	if(dev == PRI_MLC) selMLC = IS_MLC_P;	/* Primary or secondary */
	else if(dev == SEC_MLC) selMLC = IS_MLC_S;
	else return -EINVAL;

	if(on) 	MES_MLC_SetColorInversion(selMLC, layer, CTRUE, (color & 0x00ffffff));
	else 	MES_MLC_SetColorInversion(selMLC, layer, CFALSE, 0);

	MES_MLC_SetDirtyFlag(selMLC, layer);
	
	if( (tv_status == COMMAND_ONLY_TV) || (tv_status == COMMAND_COMMONVIEW) ){
	    if(on) 	MES_MLC_SetColorInversion(IS_MLC_S, layer, CTRUE, (color & 0x00ffffff));
	    else 	MES_MLC_SetColorInversion(IS_MLC_S, layer, CFALSE, 0);

	    MES_MLC_SetDirtyFlag(IS_MLC_S, layer);
	}
	
	return result;

}


static int
SetLayerAlphaBlend(u32 layer, u32 degree, u32 on, u32 dev)
{
	int result = 0;
	int selMLC;
	dprintk("SetLayerAlphaBlend (layer:%d degree:0x%08x, on:%d, mlc:%d)\n", 
								layer, degree, on, dev);

	if(dev == PRI_MLC) selMLC = IS_MLC_P;	/* Primary or secondary */
	else if(dev == SEC_MLC) selMLC = IS_MLC_S;
	else return -EINVAL;

	if( degree < 0  )  	degree = 0;
	if( degree > 15 ) 	degree = 15;
		
	if(on) 	MES_MLC_SetAlphaBlending(selMLC, layer, CTRUE, degree);
	else 	MES_MLC_SetAlphaBlending(selMLC, layer, CFALSE, 0);
	
	MES_MLC_SetDirtyFlag(selMLC, layer);
	
	if( (tv_status == COMMAND_ONLY_TV) || (tv_status == COMMAND_COMMONVIEW) ){
	    if(on) 	MES_MLC_SetAlphaBlending(IS_MLC_S, layer, CTRUE, degree);
	    else 	MES_MLC_SetAlphaBlending(IS_MLC_S, layer, CFALSE, 0);
	
	    MES_MLC_SetDirtyFlag(IS_MLC_S, layer);
    }
	
	
	return result;
}	


static int
SetDispDeviceEnable(int layer, u32 on, u32 dev)
{
	int result = 0;
	int selMLC;

	dprintk("SetDispDeviceEnable (layer:%d, on:%d, mlc:%d)\n", 
								layer, on, dev);

	if(dev == PRI_MLC) selMLC = IS_MLC_P;	/* Primary or secondary */
	else if(dev == SEC_MLC) selMLC = IS_MLC_S;
	else return -EINVAL;

	if(on)	MES_MLC_SetLayerEnable(selMLC, layer, CTRUE);
	else	MES_MLC_SetLayerEnable(selMLC, layer, CFALSE);
		
	MES_MLC_SetDirtyFlag(selMLC, layer);
	MES_MLC_SetTopDirtyFlag(selMLC);
	
	if( (tv_status == COMMAND_ONLY_TV) || (tv_status == COMMAND_COMMONVIEW) ){
        if(on)	MES_MLC_SetLayerEnable(IS_MLC_S, layer, CTRUE);
	    else	MES_MLC_SetLayerEnable(IS_MLC_S, layer, CFALSE);
		
	    MES_MLC_SetDirtyFlag(IS_MLC_S, layer);
	    MES_MLC_SetTopDirtyFlag(IS_MLC_S);
    }
	
	return result;
}


static int
SetupRgbLayer(int layer, u32 base, int left, int top, int right, int bottom, 
                            u32 hStride, u32 vStride, int en3d, u32 backcol, u32 dev)
{
	int result = 0;
	int selMLC;
	
	dprintk("SetupRgbLayer(%d) BASE:0x%08x, POS(l:%d,t:%d,r:%d,b:%d) BACK(0x%08x) (%d)\n", 
					layer, base, left, top, right, bottom, backcol, dev);

	if(dev == PRI_MLC) selMLC = IS_MLC_P;				/* Primary or secondary */
	else if(dev == SEC_MLC) selMLC = IS_MLC_S;
	else return -EINVAL;
			
	if(layer > 1)
	{
		dprintk(" Not support Rgb layer num(%d)(0,1) \r\n", layer);
		result = -EINVAL;
	}
	else
	{
		MES_MLC_SetFormat(selMLC, layer, (enum RGBFMT)DISPLAY_MLC_RGB_FORMAT);
		MES_MLC_SetPosition(selMLC, layer, left, top, right-1, bottom-1);
		if(!en3d)
		    MES_MLC_SetRGBLayerStride(selMLC, layer, DISPLAY_MLC_BYTE_PER_PIXEL, (right-left) * DISPLAY_MLC_BYTE_PER_PIXEL);
		else
		    MES_MLC_SetRGBLayerStride(selMLC, layer, hStride, vStride);
		    
		MES_MLC_SetRGBLayerAddress(selMLC, layer, base);
		MES_MLC_SetLockSize(selMLC, layer, 4);
#if 0	
	    MES_MLC_Set3DEnb(selMLC, layer, CFALSE);
#else	
		MES_MLC_Set3DEnb(selMLC, layer, en3d);
#endif
		MES_MLC_SetLayerPowerMode(selMLC, layer, CTRUE);
		MES_MLC_SetLayerSleepMode(selMLC, layer, CFALSE);
		MES_MLC_SetAlphaBlending (selMLC, layer, CFALSE, 0);
		MES_MLC_SetColorInversion(selMLC, layer, CFALSE, 0);	
		MES_MLC_SetDirtyFlag(selMLC, layer);
		MES_MLC_SetBackground(selMLC, (backcol & 0x00ffffff));
		MES_MLC_SetTopDirtyFlag(selMLC);
	}
	
	if( (tv_status == COMMAND_ONLY_TV) || (tv_status == COMMAND_COMMONVIEW) )
	{
	    MES_MLC_SetFormat(IS_MLC_S, layer, (enum RGBFMT)DISPLAY_MLC_RGB_FORMAT);
		MES_MLC_SetPosition(IS_MLC_S, layer, left, top, right-1, bottom-1);
		
		if(!en3d)
		    MES_MLC_SetRGBLayerStride(IS_MLC_S, layer, DISPLAY_MLC_BYTE_PER_PIXEL, (right-left) * DISPLAY_MLC_BYTE_PER_PIXEL);
		else
		    MES_MLC_SetRGBLayerStride(IS_MLC_S, layer, hStride, vStride);
		    
		MES_MLC_SetRGBLayerAddress(IS_MLC_S, layer, base);
		MES_MLC_SetLockSize(IS_MLC_S, layer, 4);
		MES_MLC_Set3DEnb(IS_MLC_S, layer, en3d);

		MES_MLC_SetLayerPowerMode(IS_MLC_S, layer, CTRUE);
		MES_MLC_SetLayerSleepMode(IS_MLC_S, layer, CFALSE);
		MES_MLC_SetAlphaBlending (IS_MLC_S, layer, CFALSE, 0);
		MES_MLC_SetColorInversion(IS_MLC_S, layer, CFALSE, 0);	
		MES_MLC_SetDirtyFlag(IS_MLC_S, layer);
		MES_MLC_SetBackground(IS_MLC_S, (backcol & 0x00ffffff));
		MES_MLC_SetTopDirtyFlag(IS_MLC_S);
	}
	
	return result;
}


static int GetDirtflag(int layer, u32 dev)
{
    int selMLC;
    
    if(dev == PRI_MLC) selMLC = IS_MLC_P;				/* Primary or secondary */
	else if(dev == SEC_MLC) selMLC = IS_MLC_S;
	else return -EINVAL;

#if 0	
    return MES_MLC_GetDirtyFlag(selMLC, layer);
#else
    return MES_MLC_GetDirtyFlag(selMLC, layer) | MES_MLC_GetTopDirtyFlag(selMLC);
#endif

}

static int SetRgbPower(int layer, u32 on, u32 dev)
{
	int result = 0;
	int selMLC;
	
	if(dev == PRI_MLC) selMLC = IS_MLC_P;				/* Primary or secondary */
	else if(dev == SEC_MLC) selMLC = IS_MLC_S;
	else return -EINVAL;

	if(layer > 1)
	{
		dprintk(" Not support Rgb layer num(%d)(0,1) \r\n", layer);
		result = -EINVAL;
	}
	else
	{
		if(on){
			MES_MLC_SetLayerPowerMode(selMLC, layer, CTRUE);
			MES_MLC_SetLayerSleepMode(selMLC, layer, CFALSE);
		}else{
			MES_MLC_SetLayerPowerMode(selMLC, layer, CFALSE);
			MES_MLC_SetLayerSleepMode(selMLC, layer, CTRUE);
		}
	}
	
	
	if( (tv_status == COMMAND_ONLY_TV) || (tv_status == COMMAND_COMMONVIEW) )
	{
	    if(on){
			MES_MLC_SetLayerPowerMode(IS_MLC_S, layer, CTRUE);
			MES_MLC_SetLayerSleepMode(IS_MLC_S, layer, CFALSE);
		}else{
			MES_MLC_SetLayerPowerMode(IS_MLC_S, layer, CFALSE);
			MES_MLC_SetLayerSleepMode(IS_MLC_S, layer, CTRUE);
		}
	}
	
	return result;
}

/*
 * ------------------------------------------------------------------------------
 *	IDCT functions
 * ------------------------------------------------------------------------------
 */
static int RunIdct(U16 *InputData, U16 *QuantMatrix, U16 * OutputData )
{
    MES_IDct10(InputData, QuantMatrix, OutputData);
    return 0;
}

#ifndef CONFIG_ARCH_ADD_GPH_F300

/*
 * ------------------------------------------------------------------------------
 *	lcd seting functions
 * ------------------------------------------------------------------------------
 */

static int LcdStandbyOn(void)
{
    lcd_SetW(0x05, 0x00);         
    mdelay(32);
    LCD_AVDD_OFF;
    mdelay(32);
    lcd_SetW(0x10, 0x01);
    mdelay(100);
    
    lcd_SetW( 0x22, 0xffff );	//mdelay( 100 );
    
    return 0;
}

static int LcdStandbyOff(void)
{
    lcd_SetW(0x10, 0x00);         
    mdelay(100);
    LCD_AVDD_ON;
    mdelay(32);
    lcd_SetW(0x5, 0x01);
    mdelay(32);
    
    lcd_SetW( 0x22, 0xffff );	//mdelay( 100 );
    
    return 0;
}

static int LcdGammaSet(u32 value)
{
    struct lcd_set_form *gamma = &lcd_set_form_tbl[value][lightIndex];       
    gammaIndex = value;
    lcd_SetW(0x70, gamma->r70);
    lcd_SetW(0x71, gamma->r71);
    lcd_SetW(0x72, gamma->r72);
    lcd_SetW(0x73, gamma->r73);
    lcd_SetW(0x74, gamma->r74);
    lcd_SetW(0x75, gamma->r75);
    lcd_SetW(0x76, gamma->r76);
    lcd_SetW(0x77, gamma->r77);
    lcd_SetW(0x78, gamma->r77);
    
    lcd_SetW( 0x22, 0xffff );	//mdelay( 500 );
    
    return 0;

}


static int LcdRightWrite(u32 value)
{
    struct lcd_set_form *gamma = &lcd_set_form_tbl[gammaIndex][value];
    lcd_SetW(0x70, gamma->r70);
    lcd_SetW(0x71, gamma->r71);
    lcd_SetW(0x72, gamma->r72);
    lcd_SetW(0x73, gamma->r73);
    lcd_SetW(0x74, gamma->r74);
    lcd_SetW(0x75, gamma->r75);
    lcd_SetW(0x76, gamma->r76);
    lcd_SetW(0x77, gamma->r77);
    lcd_SetW(0x78, gamma->r77);
    
    lcd_SetW( 0x22, 0xffff ); //mdelay( 500 );
    
    return 0;
}

static int LcdDirection(int flag)
{
    if(flag){
		InitializeDPC(COMMAND_LCD_INIT, 240, 320, 0, 0, 0);
        InitializeMLC(COMMAND_LCD_INIT, 320, 240, 0, 0);
		InitializeLCD(1);
    }else{
        InitializeDPC(COMMAND_LCD_INIT, 240, 320, 0, 0, 0);
        InitializeMLC(COMMAND_LCD_INIT, 240, 320, 0, 0);
		InitializeLCD(0);
    }
    
    return 0;
}


static void lcd_SetW( u16 addr, u16 Data )
{
	int i;
	u8 sByte = 0x70;
	
	if(addr == 0xffff) goto SKIP_INDEX;
#ifndef CONFIG_ARCH_ADD_GPH_F300
    while( !pollux_gpio_getpin(POLLUX_GPA11) );     //seek 'H'
    while( pollux_gpio_getpin(POLLUX_GPA11) );      //seek 'L'
#endif
	// START BYTE 
	LCD_CS_LOW;
	CountDelay(5);
	
	for(i=7 ; i>=0 ; i--){
		if( sByte & (1<<i) )	LCD_SDI_HIGH;
		else LCD_SDI_LOW;
 		
 		LCD_SCL_LOW;
		CountDelay(2);
		LCD_SCL_HIGH;
		CountDelay(2);
	}
	
	// START addr
	for(i=15 ; i>=0 ; i--){
		if( addr & (1<<i) )	LCD_SDI_HIGH;
		else LCD_SDI_LOW;
 			
		LCD_SCL_LOW;
		CountDelay(2);
		LCD_SCL_HIGH;
		CountDelay(2);
	}
	
	// END INDEX 
	LCD_CS_HIGH;
	CountDelay(5);
	if(Data == 0xffff) return ;

SKIP_INDEX:
	
	// START BYTE 
	LCD_CS_LOW;
	CountDelay(5);
	
	sByte |= 0x02;
	for(i=7 ; i>=0 ; i--){
		if( sByte & (1<<i) )	LCD_SDI_HIGH;
		else LCD_SDI_LOW;
 		
 		LCD_SCL_LOW;
		CountDelay(2);
		LCD_SCL_HIGH;
		CountDelay(2);
	}
	
	// START ADDR
	for(i=15 ; i>=0 ; i--){
		if( Data & (1<<i) )	LCD_SDI_HIGH;
		else LCD_SDI_LOW;
 	
		LCD_SCL_LOW;
		CountDelay(2);
		LCD_SCL_HIGH;
		CountDelay(2);
	}
	
	//END INSTRUCTION 
	LCD_SDI_HIGH;
	LCD_CS_HIGH;
	CountDelay(5);
}

static u16 lcd_SetR( u16 addr )
{
	u16 Data = 0;
	u8 sByte = 0x70;
	int i;
	
	LCD_CS_LOW;
	udelay(100);
	
	for(i=7 ; i>=0 ; i--){
		if( sByte & (1<<i) )	LCD_SDI_HIGH;
		else LCD_SDI_LOW;
 		
 		LCD_SCL_LOW;
		udelay(50);
		LCD_SCL_HIGH;
		udelay(50);
	}
	
	// START addr
	for(i=15 ; i>=0 ; i--){
		if( addr & (1<<i) )	LCD_SDI_HIGH;
		else LCD_SDI_LOW;
 			
		LCD_SCL_LOW;
		udelay(50);
		LCD_SCL_HIGH;
		udelay(50);
	}
	
	// END INDEX 
	LCD_SDI_HIGH;
	LCD_CS_HIGH;
	udelay(200);
	
    /* **** START BYTE ***** */
	LCD_CS_LOW;
	udelay(100);
	
	sByte |= 0x03;
	for(i=7 ; i>=0 ; i--){
		if( sByte & (1<<i) )	LCD_SDI_HIGH;
		else LCD_SDI_LOW;
 		
 		LCD_SCL_LOW;
		udelay(50);
		LCD_SCL_HIGH;
		udelay(50);
	}
	
	LCD_SDI_HIGH;
	udelay(200);
	
	// read data
	for(i=15 ; i>=0 ; i--)
	{
		LCD_SCL_LOW;
		udelay(50);
		LCD_SCL_HIGH;
		udelay(50);
		
		if(LCD_SDO_READ) Data |= (1<<i);
	}
	
	//END INSTRUCTION 
	LCD_SDI_HIGH;
	LCD_CS_HIGH;
	udelay(100);
	return Data;
}

#else  /* lg lcd */

void lcdSetWrite(unsigned char id, lcd_seting *p)
{
	int i;
	
	// START INDEX  
	CS_LOW;
	udelay(50);
	
	for(i=7 ; i>=0 ; i--){
		if( id & (1<<i) )	SDA_HIGH;
		else SDA_LOW;
 		
 		SCL_LOW;
		udelay(50);
		SCL_HIGH;
		udelay(50);
 	}
	
	for(i=15 ; i>=0 ; i--){
		if( p->reg & (1<<i) )	SDA_HIGH;
		else SDA_LOW;
 			
		SCL_LOW;
		udelay(50);
		SCL_HIGH;
		udelay(50);
	}
	
	// END INDEX 
	SDA_HIGH;
	udelay(50);
	CS_HIGH;
	udelay(50);
	udelay(50);
	udelay(50);

	// START INSTRUCTION
	CS_LOW;
	udelay(50);
	
	id |= 0x02;
	for(i=7 ; i>=0 ; i--)
	{
		if( id & (1<<i) )	SDA_HIGH;
		else SDA_LOW;
 		
 		
		SCL_LOW;
		udelay(50);
		SCL_HIGH;
		udelay(50);
	}
	
	for(i=15 ; i>=0 ; i--)
	{
		if( *(p->pVal) & (1<<i) )	SDA_HIGH;
		else SDA_LOW;
 	

		SCL_LOW;
		udelay(50);
		SCL_HIGH;
		udelay(50);
	}
	
	//END INSTRUCTION 
	SDA_HIGH;
	udelay(50);
	CS_HIGH;
	udelay(50);
}

static int LcdRightWrite(u32 value)
{
    MES_PWM_SetDutyCycle(PWM_DISPLAY_LCD_PRI_BRIGHTNESS, value);
    return 0;
} 

static int LcdStandbyOn(void)
{
   LcdRightWrite(4);
   return 0;
}

static int LcdStandbyOff(void)
{
   LcdRightWrite(lightIndex);
   return 0;
}    


static int LcdGammaSet(u32 value)
{
    return 0;
}
#endif   /* CONFIG_ARCH_POLLUX_GPH_GBOARD */


static int LcdChangeSet(u32 cmd, u32 value)
{


    switch(cmd)
    {
#ifdef CONFIG_ARCH_POLLUX_GPH_GBOARD
        
        case LCD_POWER_DOWN_ON_CMD :
            LcdStandbyOn();
            break; 
        case LCD_POWER_DOWN_OFF_CMD:
            LcdStandbyOff();
            break; 
        case LCD_GAMMA_SET_CMD :    
            LcdGammaSet(value);
            break;
        case LCD_LIGHT_SET_CMD :        
            lightIndex = value;
            LcdRightWrite(value);
            break;
        
#ifndef CONFIG_ARCH_ADD_GPH_F300       
        case LCD_DIRECTION_ON_CMD:
            LcdDirection(1);
            break;
        case LCD_DIRECTION_OFF_CMD:
            LcdDirection(0);     
            break;
#endif

#endif
        default:
            printk("Not Support command\n");
    }           

    
    return 0;
}    	

/*
 * ------------------------------------------------------------------------------
 *	tv seting functions
 * ------------------------------------------------------------------------------
 */
 
int TvConfiguration(u8 command, u8 mode, u32 width, u32 Height)
{
    
    switch(command)
    {
        case COMMAND_INDIVIDUALLY:
            tv_status = COMMAND_INDIVIDUALLY;
#if 0
            InitializeDPC(command, 320, 240, width, Height, mode);
            InitializeMLC(command, 320, 240, width, Height);
            sec_h = Height;
#endif            
            break;
        case COMMAND_COMMONVIEW:
            InitializeDPC(command, 320, 240, width, Height, mode);
            InitializeMLC(command, 320, 240, width, Height);
            tv_status = COMMAND_COMMONVIEW;
            sec_h = Height;
            break;
        case COMMAND_ONLY_TV:
            InitializeDPC(command, 320, 240, width, Height, mode);
            InitializeMLC(command, 320, 240, width, Height);
#if 1
            LcdStandbyOn();  //lcd off
            
            // mlc0 & dpc0 disable 
            MES_MLC_SetLayerEnable( 0, LAYER_DISPLAY_SCREEN_RGB, CFALSE );
		    MES_MLC_SetDirtyFlag( 0, LAYER_DISPLAY_SCREEN_RGB );
            //MES_MLC_SetClockBClkMode(0, MES_BCLKMODE_DISABLE );		
            MES_MLC_SetTopPowerMode (0, CFALSE );
	        MES_MLC_SetTopSleepMode (0, CTRUE );
            MES_MLC_SetMLCEnable( 0, CFALSE );
		    MES_MLC_SetTopDirtyFlag(0);
            
            MES_DPC_SelectModule( 0 );
	        MES_DPC_SetVideoEncoderPowerDown( CTRUE );
            MES_DPC_SetENCEnable( CFALSE );
            MES_DPC_SetClockDivisorEnable(CFALSE);	// CLKENB : Provides internal operating clock.
            MES_DPC_SetDPCEnable( CFALSE );
            
            tv_status = COMMAND_ONLY_TV;
#else
            tv_status = COMMAND_COMMONVIEW;
#endif            
            sec_h = Height;
            break;
        case COMMAND_RETURN_LCD:
            // mlc1 & dpc1 disable     
            MES_MLC_SetLayerEnable( 1, LAYER_DISPLAY_SCREEN_RGB, CFALSE );
		    MES_MLC_SetDirtyFlag( 1, LAYER_DISPLAY_SCREEN_RGB );
            //MES_MLC_SetClockBClkMode(1, MES_BCLKMODE_DISABLE );		
            MES_MLC_SetTopPowerMode (1, CFALSE );
	        MES_MLC_SetTopSleepMode (1, CTRUE );
            MES_MLC_SetMLCEnable( 1, CFALSE );
		    MES_MLC_SetTopDirtyFlag(1);
            
            MES_DPC_SelectModule( 1 );
	        MES_DPC_SetVideoEncoderPowerDown( CTRUE );
            MES_DPC_SetENCEnable( CFALSE );
            MES_DPC_SetClockDivisorEnable(CFALSE);	// CLKENB : Provides internal operating clock.
            MES_DPC_SetDPCEnable( CFALSE );
		    
		    tv_status = COMMAND_RETURN_LCD;
		    break;
        case COMMAND_SCREEN_POS:
            {
                u32 left,top,right,bottom;
                
                right = width, left = 0;
                bottom = Height, top = 0;
                
                if(width > 320 ) left = width - 320;
                else if(width < 320) {
                    left = 320 - width;                    
                    left = ~left;
                }     
                
                if(Height > 240) top = Height - 240;
                else if(Height < 240 ){
                     top = 240 - Height;
                     top = ~top;
                }
                    
                if( tv_status == COMMAND_COMMONVIEW ){
                    MES_MLC_SetPosition(1, LAYER_DISPLAY_SCREEN_RGB, left, top, right, bottom);
                    MES_MLC_SetDirtyFlag( 1, LAYER_DISPLAY_SCREEN_RGB );
                }else if(tv_status = COMMAND_ONLY_TV){
                    MES_MLC_SetPosition(1, LAYER_DISPLAY_SCREEN_RGB, left, top, right, bottom);
                    MES_MLC_SetDirtyFlag( 1, LAYER_DISPLAY_SCREEN_RGB );
                }    

            }
            break;
        case COMMAND_SCREEN_SCALE:
            MES_DPC_SetHorizontalUpScaler( CTRUE, width, Height);
            MES_MLC_SetDirtyFlag( 1, LAYER_DISPLAY_SCREEN_RGB );
            break;
        case COMMAND_CHANGE_MODE:
            InitializeDPC(command, 320, 240, 720, 480, mode);
            break; 
        case COMMAND_SET_COLOR:
            {
                u8 sch, hue, sat, crt, prt;
                MES_DPC_GetVideoEncoderColorControl( &sch, &hue, &sat, &crt, &prt );
            
                if( mode & SCH_PHASE)
                    sch = width & 0xff; 
                if( mode & HUE_PHASE)  
                    hue = (width & 0xff00) >> HUE_SHIFT;
                if( mode & CHROMA_SATURATION)
                    sat = (width & 0xff0000) >> CHROMA_SHIFT;
                if( mode & LUMA_GAIN)    
                    crt = (width & 0xff000000) >> LUMA_GAIN_SHIFT;
                if( mode & LUMA_OFFSET)    
                    crt = Height & 0xff; 
                
                MES_DPC_SetVideoEncoderColorControl(  sch, hue, sat, crt, prt);                                         
            }
            break;
        case COMMAND_SET_SYNC_TEST:
            break;
        default:
            printk("Not Support command\n");           
    }
    return 0;
}

/*
 * ------------------------------------------------------------------------------
 *	miscellaneous functions
 * ------------------------------------------------------------------------------
 */
static u16
R8G8B8toR5G6B5(u32 rgb)
{
	u8	R = (U8)((rgb>>16) & 0xff);	
	u8	G = (U8)((rgb>>8 ) & 0xff);	
	u8	B = (U8)((rgb>>0 ) & 0xff);	

	u16 R5G6B5 = ((R & 0xF8)<<8) | ((G & 0xFC)<<3) | ((B & 0xF8)>>3);

	dprintk(" RGB888:0x%08x -> RGB565:0x%08x\n", rgb, R5G6B5);	

	return R5G6B5;
}	


static u32 	
R5G6B5toR8G8B8(u16 rgb)
{
	u8 R5  = (rgb >> 11) & 0x1f;
	u8 G6  = (rgb >> 5 ) & 0x3f;
	u8 B5  = (rgb >> 0 ) & 0x1f;

	u8 R8  = ((R5 << 3) & 0xf8) | ((R5 >> 2) & 0x7);
	u8 G8  = ((G6 << 2) & 0xfc) | ((G6 >> 4) & 0x3);
	u8 B8  = ((B5 << 3) & 0xf8) | ((B5 >> 2) & 0x7);
		
	u32  R8B8G8 = (R8 << 16) | (G8 << 8) | (B8);

	dprintk(" RGB565:0x%08x -> RGB888:0x%08x\n", rgb, R8B8G8);	

	return R8B8G8;
}	


static __inline__ void SwitchVideo(int *left, int *right)
{
	// Dual display device.
#if ( DISPLAY_SEC_DISPLAY_ENABLE == CTRUE 	&&	\
	  DISPLAY_PRI_DISPLAY_ENABLE   == CTRUE	&&	\
	  DISPLAY_MAIN_DISPLAY_SCREEN 		    != DISP_ALL_MAIN_SCREEN )

	int middle = *left + ((*right - *left)>>1);	
	U32 MainScreenWidth;
	
	DISPLAY_MAIN_DISPLAY_SCREEN == VID_PRI_MAIN_DISPLAY ?	
	MainScreenWidth = DISPLAY_PRI_MAX_X_RESOLUTION :
	MainScreenWidth = DISPLAY_SEC_MAX_X_RESOLUTION ;
													
	dprintk(" SwitchVideo (l:%d, r:%d), (w:%d, m:%d)\r\n"), 
		*left, *right, MainScreenWidth, middle));

	if(middle > (int)MainScreenWidth) 
	{
		*left  -= MainScreenWidth;
		*right -= MainScreenWidth;
	}

#endif
}

static void CountDelay(int count)
{	
	volatile unsigned int delay = 0;
 	for(delay = 0; delay<count*100; delay++); 
}
