package com.antec.smartlink.miracastgui;

import com.antec.smartlink.miracast.IAmmboxService;
import com.antec.smartlink.miracast.IAmmboxServiceCallback;

import android.util.Log;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ComponentName;
import android.content.ServiceConnection;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Binder;
import android.os.RemoteException;
import android.os.Message;
import android.app.Activity;
import android.app.Service;
import android.view.View;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View.OnClickListener;
import android.widget.Button;
import android.view.ViewStub;
import android.widget.TextView;
import android.text.method.ScrollingMovementMethod;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.view.LayoutInflater;
import android.widget.LinearLayout;
import android.widget.LinearLayout.LayoutParams;
import android.view.Gravity;
import android.os.Looper;
import android.os.HandlerThread;
import android.support.v4.content.LocalBroadcastManager;
import android.os.SystemClock;

public class MiracastService extends Service {
    private final static int kWhatStatus = 3001;
    protected IAmmboxService mAmmboxService;
    boolean mServiceBound = false;
    private final static String TAG = "MiracastService";
    ServiceConnection mServiceConnection;
    static boolean threadRunning = false;
    
    private IBinder mBinder = new MyBinder();
    public class MyBinder extends Binder {
        MiracastService getService() {
            return MiracastService.this;
        }
    }
    
    @Override 
    public void onCreate() {
        Log.i(TAG,"MiracastService:onCreate");
        initConnection();
    }

    @Override
    public IBinder onBind(Intent intent) {
        Log.i(TAG, "MiracastService:onBind");
        return mBinder;
    }
    @Override
    public boolean onUnbind(Intent intent) {
        Log.i(TAG, "MiracastService:onUnbind");
        return true;
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "MiracastService:onDestroy");
        if(mServiceBound){
            try{
                mAmmboxService.unregisterCallback(mCallback);
            } catch (RemoteException e) {
                e.printStackTrace();
            }
            unbindService(mServiceConnection);
            //stopService(new Intent("com.antec.smartlink.miracast.IAmmboxService"));
        }
    }

    /*Note
        MiracastService functions exported.
    */
    public void create(){
        try{
            if(mServiceBound){
                Log.i(TAG, "MiracastService:create");
                mAmmboxService.create();
            }
        }catch (RemoteException e){
            e.printStackTrace();
        }
        return ;
    }
    public void destroy(){
        try{
            if(mServiceBound){
                Log.i(TAG, "MiracastService:destroy");
                mAmmboxService.destroy();
                unbindService(mServiceConnection);
                //stopService(new Intent("com.antec.smartlink.miracast.IAmmboxService"));
            }
        }catch (RemoteException e){
            e.printStackTrace();
        }
        return ;
    } 
    public void play(){
        if(mServiceBound){
            try{
                if(mAmmboxService.getState() == 1){
                    Log.i(TAG, "MiracastService:play");
                    mAmmboxService.play();
                    
                }
            }catch (RemoteException e){
                e.printStackTrace();
            }
        }
        return ;
    }
    
    public void pause(){
        try{
            if (mServiceBound) {
                mAmmboxService.pause();
                
            }
        }catch (RemoteException e){
            e.printStackTrace();
        }

        return ;
    }

    public void suspend(){
        /*
        try{
            if (mServiceBound) {
                mAmmboxService.suspend();
                
            }
        }catch (RemoteException e){
            e.printStackTrace();
        }
        */
        return ;
    }
    
    public void mute(){
        try{
            if (mServiceBound) {
                mAmmboxService.mute();
            }
        }catch (RemoteException e){
            e.printStackTrace();
        }
        return ;
    }
    public void unmute(){
        try{
            if (mServiceBound) {
                mAmmboxService.unmute();
            }
        }catch (RemoteException e){
            e.printStackTrace();
        }
        return ;
    }
    public int getstate(){
        int state = 0;
        try{
            if (mServiceBound) {
                state = mAmmboxService.getState();
            }
        }catch (RemoteException e){
            e.printStackTrace();
        }
        return state;
    }
    public void setScale(int type){
        try{
            if (mServiceBound) {
                mAmmboxService.setScale(type);
            }
        }catch (RemoteException e){
            e.printStackTrace();
        }
        return ;
    }
    public void setAudioPath(int path){
        try{
            if (mServiceBound) {
                mAmmboxService.setAudioPath(path);
            }
        }catch (RemoteException e){
            e.printStackTrace();
        }
        return ;
    }
    public void setVolume(int vol){
        try{
            if (mServiceBound) {
                mAmmboxService.setVolume(vol);
            }
        }catch (RemoteException e){
            e.printStackTrace();
        }
        return ;
    }

    public void setSurface(Surface s){
         try{
            if(mServiceBound &&
               mAmmboxService.getState() == 1){
                Log.i(TAG, "MiracastService:setSurface");
                mAmmboxService.setSurface(s);
            }
        }catch (RemoteException e){
            e.printStackTrace();
        }
        return ;
    }
    public void setTouchEvent(int type,int x, int y, int w, int h){
        try{
            if(mServiceBound) {
                mAmmboxService.setTouchEvent(type,x,y,w,h);
            }
        }catch (RemoteException e){
            e.printStackTrace();
        }
        return ;
    }
    public void setInitSpeaker(int type){
        try{
            if (mServiceBound) {
                mAmmboxService.setInitSpeaker(type);
            }
        }catch (RemoteException e){
            e.printStackTrace();
        }
        return ;
    }
    public void sendKeyEvent(int type, int keyCode1, int keyCode2){
        try{
            if(mServiceBound) {
                mAmmboxService.sendKeyEvent(type, keyCode1, keyCode2);
                
            }
        }catch (RemoteException e){
            e.printStackTrace();
        }
        return ;
    }
    

    void initConnection(){
        mServiceConnection = new ServiceConnection() {
            
            //onServiceDisconnected is only called in extreme situations (crashed / killed).
            //Never be called.
            @Override
            public void onServiceDisconnected(ComponentName name) {
                Log.i(TAG,"MiracastService:onServiceDisconnected");
                mServiceBound = false;
            }

            @Override
            public void onServiceConnected(ComponentName name, IBinder service) {
                Log.i(TAG,"MiracastService:onServiceConnected");
                mAmmboxService = IAmmboxService.Stub.asInterface((IBinder) service);
                
                try {
                    mAmmboxService.registerCallback(mCallback);
                } catch (RemoteException e) {
                    e.printStackTrace();
                }
                mServiceBound = true;
            }
        };
        if(mServiceBound == false){
            Intent it = new Intent();
            it.setAction("com.antec.smartlink.miracast.IAmmboxService");
            bindService(it, mServiceConnection, Service.BIND_AUTO_CREATE);

        }

    }
    private IAmmboxServiceCallback mCallback = new IAmmboxServiceCallback.Stub() {

        @Override
        public void handlerStatusEvent(int msgID, String param) throws RemoteException {
            Message msg = new Message();
            msg.what = msgID;
            msg.obj = param;
            if(mHandler != null)
                mHandler.sendMessage(msg);  
        }
    };
    private final Handler mHandler = new Handler(){
        @Override
        public void handleMessage(Message msg) {
            
            switch(msg.what){
                case kWhatStatus:
                    String text = (String)msg.obj;
                    sendStatusReport(text);
                    break;
                
                default:
                    break;
            }
        }
    };
    private void sendStatusReport(String msg) {
        if(msg.equalsIgnoreCase("SHUTDOWN")){
            restartAmmboxService();
        }
        //Broadcast status report message to MainActivity
        Intent intent = new Intent("AmmboxStatusReport");
        intent.putExtra("STATUS", msg);
        LocalBroadcastManager.getInstance(this).sendBroadcast(intent);
    }

    private void restartAmmboxService(){
        Log.i(TAG,"MiracastService:restartAmmboxService");
        stopService(new Intent("com.antec.smartlink.miracast.IAmmboxService"));
        SystemClock.sleep(5000);
        startService(new Intent("com.antec.smartlink.miracast.IAmmboxService")); 
    }

    
    
}
