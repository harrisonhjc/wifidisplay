package com.antec.smartlink.miracastgui;


import android.util.Log;
import android.util.DisplayMetrics;
import android.support.v4.content.LocalBroadcastManager;
import android.text.method.ScrollingMovementMethod;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ComponentName;
import android.content.ServiceConnection;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.RemoteException;
import android.app.Activity;
import android.app.Service;
import android.app.ActionBar;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.graphics.SurfaceTexture;
import android.graphics.Color;
import android.widget.LinearLayout;
import android.widget.Button;
import android.widget.TextView;
import android.widget.RelativeLayout;
import android.widget.LinearLayout.LayoutParams;
import android.view.LayoutInflater;
import android.view.Gravity;
import android.view.Window;
import android.view.WindowManager;
import android.view.TextureView;
import android.view.View.OnTouchListener;
import android.view.MotionEvent;
import android.view.SurfaceHolder.Callback;
import android.view.ViewStub;
import android.view.View;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View.OnClickListener;
import android.view.KeyEvent;


import com.antec.smartlink.miracastgui.MiracastService.MyBinder;

public class MainActivity extends Activity implements TextureView.SurfaceTextureListener,OnTouchListener{
        
    private static final String TAG = "AmmboxSampleGUI";
    private static boolean mWfdSinkPlaying = false;
    private static TextureView mTextureView;
    private static SurfaceTexture mSavedSurfaceTexture;
    private Surface mSurface;
    private static int mVolume = 0; // 0 ~ 100
    private static int viewWidth;
    private static int viewHeight;
    private static String mState;

    MiracastService MiracastService;
    boolean mMiracastServiceBound = false;
    private boolean mSurfaceReady = false;
    private static int audiopathType = 10;
    
    TextView mStatusView;
    Button playButton;
    Button pauseButton;
    
    float xRatio;
    float yRatio;
    
    Handler mHandler = new Handler();
    
    @Override 
    public void onCreate(Bundle savedInstanceState) {

        super.onCreate(savedInstanceState);
        Log.i(TAG,"AmmboxSampleGUI:onCreate");
        setContentView(R.layout.main);
        
        bindMiracastService();

        //to receive status report from MiracastService
        LocalBroadcastManager.getInstance(this).registerReceiver(mMessageReceiver,
                                                                 new IntentFilter("AmmboxStatusReport"));
       
        mStatusView = (TextView)findViewById(R.id.status);
        if(mState == null)
            mStatusView.append("Waiting\r\n");
        else
            mStatusView.append(mState + "\r\n");
            
        mStatusView.setTextSize(16);
        mStatusView.setTextColor(Color.parseColor("#F7540E"));

        Button volumeupButton = (Button) findViewById(R.id.volumeup);
        volumeupButton.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                if (mMiracastServiceBound) {
                    mVolume -= 10;
                    if(mVolume < 0)
                        mVolume = 0;

                    MiracastService.setVolume(mVolume);
                }
            }
        });

        Button volumedownButton = (Button) findViewById(R.id.volumedown);
        volumedownButton.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                if (mMiracastServiceBound) {
                    mVolume += 10;
                    if(mVolume > 100)
                        mVolume = 100;

                    MiracastService.setVolume(mVolume);
                }
            }
        });
        Button setAudiopathButton = (Button) findViewById(R.id.audioswitch);
        setAudiopathButton.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                if (mMiracastServiceBound) {
                    
                    if(audiopathType == 10){
                        audiopathType = 3;

                    }else{
                        audiopathType = 10;
                    }
                    MiracastService.setAudioPath(audiopathType);
                }
            }
        });

        Button HomeKeyButton = (Button) findViewById(R.id.home);
        HomeKeyButton.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                /*
                Thread inputThread = null;
                inputThread = new Thread(new InputThread());
                inputThread.start();
                */
                
                int type;
                int keyCode1;
                int keyCode2;
                int highByte = 0;
                int lowByte = 0;
                
                keyCode1 = 0;
                highByte = 0;
                lowByte = 0x04;
                keyCode2 = (highByte<<8) | lowByte;
                type = 3;
                MiracastService.sendKeyEvent(type,keyCode1,keyCode2);
                type = 4;
                MiracastService.sendKeyEvent(type,keyCode1,keyCode2);
                
            }
        });
        Button BackKeyButton = (Button) findViewById(R.id.back);
        BackKeyButton.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                int type;
                int keyCode1;
                int keyCode2;
                int highByte = 0;
                int lowByte = 0;
                
                keyCode1 = 0;
                highByte = 0;
                lowByte = 0x03;
                keyCode2 = (highByte<<8) | lowByte;
                type = 3;
                MiracastService.sendKeyEvent(type,keyCode1,keyCode2);
                type = 4;
                MiracastService.sendKeyEvent(type,keyCode1,keyCode2);
            }
        });

        Button exitButton = (Button) findViewById(R.id.exit);
        exitButton.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                finish();
            }
        });

        
        //ADD:20160812
        recreateView((TextureView)findViewById(R.id.mira_textureview));

    }


    @Override
    protected void onDestroy() {
        super.onDestroy();
        Log.i(TAG,"AmmboxSampleGUI:onDestroy");
        LocalBroadcastManager.getInstance(this).unregisterReceiver(mMessageReceiver);
        if (mMiracastServiceBound) {
            //MiracastService.suspend();
            unbindMiracastService();
            mMiracastServiceBound = false;
            MiracastService = null;
        }

    }

    @Override
    protected void onStart() {
        super.onStart();
        Log.i(TAG,"AmmboxSampleGUI:onStart");
    }

    /** Called when the activity has become visible. */
    @Override
    protected void onResume() {
       super.onResume();
       Log.i(TAG,"AmmboxSampleGUI:onResume");
    }

    /** Called when another activity is taking focus. */
    @Override
    protected void onPause() {
        super.onPause();
        Log.i(TAG,"AmmboxSampleGUI:onPause");
        if(mWfdSinkPlaying == false){
            Log.i(TAG,"AmmboxSampleGUI:onPause:free resources");
            //Notify onSurfaceTextureDestroyed() to tell TextureView to free the SurfaceTexture.
            mSavedSurfaceTexture = null;
            //free resource
            //mSurface.release();
        }
    }

    /** Called when the activity is no longer visible. */
    @Override
    protected void onStop() {
       super.onStop();
       Log.i(TAG,"AmmboxSampleGUI:onStop");
    }

    //status monitor thread
    class InputThread implements Runnable {
        int count;
        int type;
        int keyCode1;
        int keyCode2;
        int highByte;
        int lowByte;

        public void run() {
            highByte = 0;
            count = 0;
            Log.i(TAG,"AmmboxSampleGUI:InputThread start.");
            while(highByte<=0xFF){
                if (mMiracastServiceBound) {
                    for(lowByte = 0;lowByte <= 0xFF ; lowByte++){
                            keyCode2 = (highByte<<8) | lowByte;
                            keyCode1 = 0;
                            type = 3;
                            MiracastService.sendKeyEvent(type,keyCode1,keyCode2);
                            type = 4;
                            MiracastService.sendKeyEvent(type,keyCode1,keyCode2);
                            try {
                                Thread.sleep(50);
                            }catch (InterruptedException e) {
                                Log.e(TAG, "sleep error");  
                            }   

                            Log.i(TAG,"AmmboxSampleGUI:InputThread:count:H:L:K="+count+":"+highByte+":"+lowByte+":"+keyCode2);
                            count++;
                    }
                    highByte++;
                }
            }
            Log.i(TAG,"AmmboxSampleGUI:InputThread end.");
        }
    }


    @Override
    public boolean onTouch(View v, MotionEvent event) {
        
        float  x,y;
        
        switch(event.getAction()){
        case MotionEvent.ACTION_DOWN:
            x = event.getX();
            y = event.getY();
            //Log.d(TAG,"xRatio="+xRatio+",x="+x+",y="+y);
            if (mMiracastServiceBound){
                //Log.i(TAG,"x="+x+",y="+y);
                MiracastService.setTouchEvent(0, Math.round(x), Math.round(y),viewWidth,viewHeight);
            }
            break;
        
        case MotionEvent.ACTION_UP:
            x = event.getX();
            y = event.getY();
            //Log.d(TAG,"yRatio="+yRatio+",x="+x+",y="+y);
            if (mMiracastServiceBound){
                //Log.i(TAG,"x="+x+",y="+y);
                MiracastService.setTouchEvent(1, Math.round(x), Math.round(y),viewWidth,viewHeight);
            }
            break;
        

        case MotionEvent.ACTION_MOVE:
            x = event.getX();
            y = event.getY();
            //Log.d(TAG,"yRatio="+yRatio+",x="+x+",y="+y);
            if (mMiracastServiceBound){
                //Log.i(TAG,"x="+x+",y="+y);
                MiracastService.setTouchEvent(2, Math.round(x), Math.round(y),viewWidth,viewHeight);
            }
            break;
        }

        return true;
    }

    //ADD:20160812
    //////////////////////////////////////////////////////////////////////////////////////////
    public void recreateView(TextureView view) {
            Log.i(TAG, "receateView:" + view);
            mTextureView = view;

            //for uibc  touch event
            mTextureView.setOnTouchListener(this);
            
            mTextureView.setSurfaceTextureListener(this);
            if(mSavedSurfaceTexture != null) {
                Log.i(TAG, "using saved st=" + mSavedSurfaceTexture);
                view.setSurfaceTexture(mSavedSurfaceTexture);
            }
    }
    @Override
    public void onSurfaceTextureAvailable(SurfaceTexture st, int width, int height) {
        Log.i(TAG, "onSurfaceTextureAvailable size=" + width + "x" + height + ", st=" + st);

        mSurfaceReady = true;
        //uibc
        viewWidth = width;
        viewHeight = height;
        Log.i(TAG,"surfaceview:w="+viewWidth+",h="+viewHeight);

        if (mSavedSurfaceTexture == null) {
            mSavedSurfaceTexture = st;
            mSurface = new Surface(st);
        }
    }

    @Override
    public void onSurfaceTextureSizeChanged(SurfaceTexture st, int width, int height) {
        Log.i(TAG, "onSurfaceTextureSizeChanged size=" + width + "x" + height + ", st=" + st);
        //uibc
        viewWidth = width;
        viewHeight = height;
    }

    @Override
    public boolean onSurfaceTextureDestroyed(SurfaceTexture st) {
        Log.i(TAG, "onSurfaceTextureDestroyed st=" + st);
        
        mSurfaceReady = false;
        return (mSavedSurfaceTexture == null);
    }

    @Override
    public void onSurfaceTextureUpdated(SurfaceTexture st) {
        //Log.i(TAG, "onSurfaceTextureUpdated st=" + st);
    }
    /////////////////////////////////////////////////////////////////////////////////////////

    void bindMiracastService(){
        Intent intent = new Intent(this, MiracastService.class);
        bindService(intent, mServiceConnection, Context.BIND_AUTO_CREATE);
    }

    void unbindMiracastService(){
        unbindService(mServiceConnection);
    }

    /*Note:
        mServiceConnection : connect to local service:MiracastService.
        MiracastService will automatically bind to AmmboxService to manage P2P connection.
        Miracast service provides these function members below:
        create():create WFD sink.
        destroy() : destroy AmmboxService.
        play():
        pause():
        mute():
        unmute():
        getstate() : check if AmmboxService is ready.
        setSurface(s):set display window for WFD Sink.
    */
    private ServiceConnection mServiceConnection = new ServiceConnection() {
        //onServiceDisconnected is only called in extreme situations (crashed / killed).
        //Never be called.
        @Override
        public void onServiceDisconnected(ComponentName name) {
            mMiracastServiceBound = false;
            MiracastService = null;
            Log.i(TAG,"AmmboxSampleGUI:onServiceDisconnected");
        }

        @Override
        public void onServiceConnected(ComponentName name, IBinder service) {
            Log.i(TAG,"AmmboxSampleGUI:onServiceConnected");
            MyBinder myBinder = (MyBinder) service;
            MiracastService = myBinder.getService();
            mMiracastServiceBound = true;

            /*Note:
                create WFD sink instance.
            */
            MiracastService.create();
            //check if service is ready.
            mHandler.post(checkServiceThread);

            
        }
    };

    
    Runnable checkServiceThread = new Runnable(){
        public void run(){
            int state = MiracastService.getstate();
            if(state == 0){
                //service is not ready , check again after 1 second.
                mHandler.postDelayed(checkServiceThread, 1000);
            }else
            if(state ==1){
                mHandler.removeCallbacks(checkServiceThread);
                //service is ready , go to show content.
                /*Note:
                MiracastService.setSurface will trigger WFD Sink presentation on screen.
                WFD Sink needs surface to display.
                */
                if(mSurfaceReady){
                    MiracastService.setInitSpeaker(audiopathType);
                    MiracastService.setSurface(mSurface);
                    
                }
            }
        }
    };

   
    /*Note
        handleMessage:Show the AmmboxService connection status report on the textview. 
    */
    private void handleStatusReport(String state)
    {
        Log.i(TAG,"AmmboxSampleGUI:handleMessage:" + state);
        /*Note:
            state will be one of the list below: Data type is String.
            state = "CONNECTING"; 
            state = "CONNECTED";
            state = "PAUSED";
            state = "PLAYING";
            state = "PLAYING_MUTE";
            state = "PLAYING_UNMUTE";
            state = "DISCONNECTED";
            state = "SHUTDOWN"
            state = "UIBC_ENABLED"
        */
        mState = state;
        Log.i(TAG,"AmmboxSampleGUI:status:" + mState);
        mStatusView.append(mState);
        mStatusView.append("\r\n");
        mStatusView.setMovementMethod(new ScrollingMovementMethod());
        /*Note:
            The miracast connection has already closed and disconnected.
            --> Close MiracastService and MainActivity.
        */
        if(mState.equalsIgnoreCase("UIBC_ENABLED")){
            Log.i(TAG,"AmmboxSampleGUI:UIBC enabled.");
            
        }else
        if(mState.equalsIgnoreCase("ABORT")){
            Log.i(TAG,"AmmboxSampleGUI:ABORT");
            finish();
        }else
        if(mState.equalsIgnoreCase("PLAYING")){
            mWfdSinkPlaying = true;
            
        }else
        if(mState.equalsIgnoreCase("DISCONNECTED")){
            Log.i(TAG,"AmmboxSampleGUI:DISCONNECTED");
            
            mWfdSinkPlaying = false;
            finish();
        }
       
    }    
    private BroadcastReceiver mMessageReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if(intent.getAction() != null && intent.getAction().equals("AmmboxStatusReport")){
                String status = intent.getStringExtra("STATUS");
                handleStatusReport(status);
            }
        }
    };
            
}
