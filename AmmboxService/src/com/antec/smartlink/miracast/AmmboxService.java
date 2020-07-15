package com.antec.smartlink.miracast;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Timer;
import java.util.TimerTask;
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.net.ServerSocket;
import java.net.Socket;
import android.util.Log;
import android.content.ContextWrapper;
import android.content.ActivityNotFoundException;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.net.NetworkInfo;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.net.wifi.p2p.WifiP2pDevice;
import android.net.wifi.p2p.WifiP2pDeviceList;
import android.net.wifi.p2p.WifiP2pGroup;
import android.net.wifi.p2p.WifiP2pInfo;
import android.net.wifi.p2p.WifiP2pManager;
import android.net.wifi.p2p.WifiP2pManager.Channel;
import android.net.wifi.p2p.WifiP2pManager.ChannelListener;
import android.net.wifi.p2p.WifiP2pManager.ConnectionInfoListener;
import android.net.wifi.p2p.WifiP2pManager.GroupInfoListener;
import android.net.wifi.p2p.WifiP2pManager.PeerListListener;
import android.net.wifi.p2p.WifiP2pWfdInfo;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.os.RemoteCallbackList;
import android.os.RemoteException;
import android.os.Binder;
import android.os.IBinder;
import android.os.UserHandle;
import android.provider.Settings;
import android.provider.Settings;
import android.app.Service;
import android.view.Surface;
import android.view.SurfaceView;
import android.view.View;
import android.view.LayoutInflater;
import android.graphics.Canvas;
import android.content.Intent;

public class AmmboxService extends Service {
    private final static String TAG = "AmmboxService";
    private final static int kWhatStatus = 3001;
    private BroadcastReceiver mReceiver;
    private boolean mIsWiFiDirectEnabled;
    private WifiP2pManager mWifiP2pManager;
    private Channel mChannel;
    private List<WifiP2pDevice> mPeers = new ArrayList<WifiP2pDevice>();
    private ActionListenerAdapter mActionListenerAdapter;
    private String mSelectedDevice;
    private boolean mIsAppBoot;
    private final static int WfdCmd_Pause = 0;
    private final static int WfdCmd_Resume = 1;
    private final static int WfdCmd_Mute = 2;
    private final static int WfdCmd_Unmute = 3;
    private int mArpRetryCount = 0;
    private final int MAX_ARP_RETRY_COUNT = 60;
    private int mP2pControlPort = -1;
    private String mP2pInterfaceName;
    static Surface mSurface;
    private ServerSocket serverSocket;
    public static final int SERVERPORT = 45346;
    Thread serverThread = null;
    static String mDeviceName;
    static Thread mWfdSinkThread = null;
    public int mConnectState = 0;
    private Timer mArpTableObservationTimer;
    private Timer mP2pDiscoverTimer; 
    private P2pDiscoverTask mP2PTask;
    RemoteCallbackList<IAmmboxServiceCallback> mCallbacks;
        

    private final Handler mHandler = new Handler(); /*{
        @Override 
        public void handleMessage(Message msg) {
            switch(msg.what){
                case kWhatStatus:{
                    sendMsg(msg.what);
                } break;
                
                default:
                    super.handleMessage(msg);
            }
        }
    };*/
    public void sendCallbackMsg(int msgId,String text) {
        //Log.i(TAG,"AmmboxService:sendCallbackMsg");
        // Broadcast to all clients the new value.
        final int N = mCallbacks.beginBroadcast();
        try {
            for (int i=0; i<N; i++) {
                //Log.i(TAG,"AmmboxService:sendCallbackMsg 1");
                mCallbacks.getBroadcastItem(i).handlerStatusEvent(msgId, text);
            }
            
        } catch (RemoteException e) {
            // The RemoteCallbackList will take care of removing
            // the dead object for us.
        }
        mCallbacks.finishBroadcast();
        
    }

    private final IAmmboxService.Stub mBinder = new IAmmboxService.Stub() {
        public void create(){
            Log.i(TAG, "AmmboxService:create");
            return ;
        }
        public void destroy(){
            //addLog("AmmboxService:destroy");
            mWifiP2pManager.stopPeerDiscovery(mChannel, mActionListenerAdapter);
            unRegisterBroadcastReceiver();
            stopSelf();
            return ;
        }
        public void pause(){
            ammboxWfdSinkCmd(WfdCmd_Pause);   
            return ;
        }
        public void play(){
            //Log.i(TAG,"AmmboxService:onPlay");
            ammboxWfdSinkCmd(WfdCmd_Resume);   
            return ;
        }
        public void mute(){
            ammboxWfdSinkCmd(WfdCmd_Mute);   
            return ;
        }
        public void unmute(){
            ammboxWfdSinkCmd(WfdCmd_Unmute);   
            return ;
        }
        public void setSurface(Surface s){
            Log.i(TAG,"AmmboxService:setSurface");
           
            mSurface = s;  
            ammboxWfdSinkShow(mSurface);
            return ;
        }
        public int getState(){
            Log.i(TAG, "AmmboxService:getState");
            return mConnectState;
        }
        public void suspend(){
            /*
            try {  
                    FileOutputStream out = openFileOutput("wfd_suspend", MODE_APPEND);  
                    out.close();
                } catch (Exception e) {  
                    System.err.println("Error writing to wfd_suspend.");  
                }  
            */
            ammboxWfdSinkSuspend();   
            return ;
        }
        public void setScale(int type){
           addLog("AmmboxService:setScale:type:" + type);
           /*
            if(mDeviceName.contains("Xperia") ||
               mDeviceName.contains("S6")){
                ammboxWfdSinkSetScale(type);
            }else{
                if(type == 1){ //CROP
                    try {  
                        FileOutputStream out = openFileOutput("wfd_scale_crop", MODE_APPEND);  
                        out.close();
                    } catch (Exception e) {  
                        System.err.println("Error writing to wfd_scale_crop.");  
                    }  
                }else
                if(type == 0){ //window
                    try {  
                        FileOutputStream out = openFileOutput("wfd_scale_window", MODE_APPEND);  
                        out.close();
                    } catch (Exception e) {  
                        System.err.println("Error writing to wfd_scale_window.");  
                    }  
                }

            }*/
            
            return ;
        }
        public void setAudioPath(int path){
            addLog("AmmboxService:setAudioPath:path:" + path);
            ammboxWfdSinkSetAudioPath(path);
            return ;
        }
        public void setVolume(int vol){
            //addLog("AmmboxService:setvolume:" + vol);
            //ammboxWfdSinkSetVolume(vol);
            return ;
        }

        public void registerCallback(IAmmboxServiceCallback cb) throws RemoteException {
            Log.i(TAG,"AmmboxService:registerCallback");
            if (cb != null) mCallbacks.register(cb);
        }
        
        public void unregisterCallback(IAmmboxServiceCallback cb) throws RemoteException {
            
            if (cb != null) mCallbacks.unregister(cb);
        }

        public void setTouchEvent(int type,int x, int y, int w, int h){
            //Log.i(TAG,"AmmboxService:w:h=" + w + ":" +h);
            ammboxWfdSinkTouchEvent(type,x,y,w,h);
            return ;
        }

        public void setInitSpeaker(int type){
            addLog("AmmboxService:setInitSpeaker:type:" + type);
            ammboxWfdSinkSetAudioPath(type);
            /*
            //delete old files first
            File file = new File("/data/data/com.antec.smartlink.miracast/wfd_audio_streamtype_main");
            file.delete();
            
            if(type == 3){ //main speaker
                File new_file =new File("/data/data/com.antec.smartlink.miracast/wfd_audio_streamtype_main");
                try{
                   new_file.createNewFile();
                }
                catch (IOException e){
                   e.printStackTrace();
                }     
            } 
            */
            return ;
        }
        
        public void sendKeyEvent(int type, int keyCode1, int keyCode2){
            //Log.i(TAG,"sendHomeKey.");
            ammboxWfdSinkKeyEvent(type, keyCode1, keyCode2);
            return ;
        }

        
        
    };

    @Override
    public IBinder onBind(Intent intent) {
        Log.i(TAG, "AmmboxService:onBind");
        return mBinder;
        
    }
    @Override
    public int onStartCommand(Intent pIntent, int flags, int startId) {
        // TODO Auto-generated method stub
        Log.i(TAG,"AmmboxService:onStartCommand");
        return super.onStartCommand(pIntent, flags, startId);
    }
       
    @Override 
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "AmmboxService:onCreate");

        /*
        File new_file =new File("/data/data/com.antec.smartlink.miracast/wfd_audio_streamtype_main");
        try{
           new_file.createNewFile();
        }
        catch (IOException e){
           e.printStackTrace();
        }
        File file = new File("/data/data/com.antec.smartlink.miracast/wfd_audio_streamtype_main");
        file.delete();
        */

        //reset wifi 
        WifiManager wifi = (WifiManager) getSystemService(Context.WIFI_SERVICE);
        wifi.setWifiEnabled(false);
        try {
         Thread.sleep(500);
        } catch (InterruptedException e) {
         Log.e(TAG, "sleep error");  
        }    
        wifi.setWifiEnabled(true);


        mCallbacks = new RemoteCallbackList<IAmmboxServiceCallback>();
        //mHandler = new Handler();


        //enable WiFi-Display
        int wfdSettings;
        wfdSettings = Settings.Global.getInt(getContentResolver(),Settings.Global.WIFI_DISPLAY_ON,0);
        Settings.Global.putInt(getContentResolver(),Settings.Global.WIFI_DISPLAY_ON, 1);
        wfdSettings = Settings.Global.getInt(getContentResolver(),Settings.Global.WIFI_DISPLAY_ON,0);
        mIsWiFiDirectEnabled = false;
        mIsAppBoot = true;
        registerBroadcastReceiver();
        
        this.serverThread = new Thread(new ServerThread());
        this.serverThread.start();

        //create p2p discover timer
        mP2pDiscoverTimer = new Timer();
        mP2PTask = new P2pDiscoverTask();
        //mP2pDiscoverTimer.schedule(mP2PTask, 5000, 120000);
        mP2pDiscoverTimer.schedule(mP2PTask, 2000, 10000);

        
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "AmmboxService:onDestroy");
        mCallbacks.kill();
        mP2pDiscoverTimer.cancel();
        System.exit(0);
    }

    


    //status monitor thread
    class ServerThread implements Runnable {
        public void run() {
            Socket socket = null;
            try {
                serverSocket = new ServerSocket(SERVERPORT);
            } catch (IOException e) {
                e.printStackTrace();
            }
            while(!Thread.currentThread().isInterrupted()) {
                try {
                    socket = serverSocket.accept();
                    CommunicationThread commThread = new CommunicationThread(socket);
                    new Thread(commThread).start();
                    Log.d(TAG,"Status Report:new connection.");
                } catch (IOException e) {
                    e.printStackTrace();
                }
            }
        }
    }

    private void sendUpdateMessage(String msg) {
        /*
        Log.d(TAG, "sendUpdateMessage:" + msg);
        Intent i = new Intent("AmmboxService");
        i.putExtra("WFDCMD",msg);
        sendBroadcastAsUser(i, new UserHandle(UserHandle.USER_CURRENT));
        */
        sendCallbackMsg(kWhatStatus,msg);
    }   

    class CommunicationThread implements Runnable {
        
        private Socket clientSocket;
        private BufferedReader input;
        
        
        public CommunicationThread(Socket clientSocket) {
            this.clientSocket = clientSocket;
            try {
                this.input = new BufferedReader(new InputStreamReader(this.clientSocket.getInputStream()));
                
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
        public void run() {
            while(!Thread.currentThread().isInterrupted()) {
                try {
                    String read = input.readLine();
                     if (read == null ){
                        Log.d(TAG,"AmmboxService:status from WFD sink:read null.");
                        Thread.currentThread().interrupt();
                     }else{
                        sendUpdateMessage(read);
                       
                     }
                } catch (IOException e) {
                    e.printStackTrace();
                }
            }
        }
    }

    private void registerBroadcastReceiver() {
        IntentFilter filter = new IntentFilter();
        filter.addAction(WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION);
        filter.addAction(WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION);
        filter.addAction(WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION);
        filter.addAction(WifiP2pManager.WIFI_P2P_THIS_DEVICE_CHANGED_ACTION);
        mReceiver = new WiFiDirectBroadcastReceiver();
        registerReceiver(mReceiver, filter);
        //addLog("registerBroadcastReceiver() BroadcastReceiver");
    }

    
    private void unRegisterBroadcastReceiver() {
        if (mReceiver != null) {
            unregisterReceiver(mReceiver);
            mReceiver = null;
            //addLog("unRegisterBroadcastReceiver() BroadcastReceiver");
        }
    }

     private void addLog(String log) {
        Log.d(TAG, log);
    }

    private String toStringDevice(WifiP2pDevice device) {
        String log = separateCSV(device.toString()) + ":" + getDeviceStatus(device.status);
        return  log;
    }

    private String separateCSV(String csvStr) {
        return csvStr.replaceAll("[^:yWFD] ", "　");
        
    
    }

    
    private String getDeviceStatus(int deviceStatus) {
        String status = "";
        switch (deviceStatus) {
            case WifiP2pDevice.AVAILABLE:
                status = "Available";
                break;
            case WifiP2pDevice.INVITED:
                status = "Invited";
                break;
            case WifiP2pDevice.CONNECTED:
                status = "Connected";
                break;
            case WifiP2pDevice.FAILED:
                status = "Failed";
                break;
            case WifiP2pDevice.UNAVAILABLE:
                status = "Unavailable";
                break;
            default:
                status = "Unknown";
                break;
        }
        return "["+status+"]";
    }

    class ActionListenerAdapter implements WifiP2pManager.ActionListener {

     
        public void onSuccess() {
            //String log = " onSuccess()";
            //addLog(log);
        }

        
        public void onFailure(int reason) {
            String log = " onFailure("+getReason(reason)+")";
            addLog(log);
        }

        
        private String getReason(int reason) {
            String[] strs = {"ERROR", "P2P_UNSUPPORTED", "BUSY"};
            try {
                return strs[reason] + "("+reason+")";
            } catch (ArrayIndexOutOfBoundsException e) {
                return "UNKNOWN REASON CODE("+reason+")";
            }
        }
    }

    private boolean isNull(boolean both) {
        if (mActionListenerAdapter == null) {
            mActionListenerAdapter = new ActionListenerAdapter();
        }

        if (!mIsWiFiDirectEnabled) {
            addLog(" Wi-Fi Direct is OFF!try Setting Menu");
            return true;
        }

        if (mWifiP2pManager == null) {
            addLog(" mWifiP2pManager is NULL! try getSystemService");
            return true;
        }
        if (both && (mChannel == null) ) {
            addLog(" mChannel is NULL!try initialize");
            return true;
        }

        return false;
    }

   
    public void GetSystemService() {
        mWifiP2pManager = (WifiP2pManager) getSystemService(Context.WIFI_P2P_SERVICE);

       addLog("WiFi P2P Service:["+(mWifiP2pManager != null)+"]");
    }

   
    public void Initialize() {
        
        if (isNull(false)) { return; }

        mChannel = mWifiP2pManager.initialize(this, getMainLooper(), new ChannelListener() {
            public void onChannelDisconnected() {
                addLog("mWifiP2pManager.initialize() -> onChannelDisconnected()");
            }
        });

        //addLog("　Result["+(mChannel != null)+"]");
    }

    
    public void DiscoverPeers() {
        
        if (isNull(true)) { return; }

        mWifiP2pManager.discoverPeers(mChannel, mActionListenerAdapter);
        //addLog("Discovering peers ...");
    }

    public class WiFiDirectBroadcastReceiver extends BroadcastReceiver {

        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            String log = "onReceive() ["+action+"]";
            //addLog(log);

            if (WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION.equals(action)) {
                mIsWiFiDirectEnabled = false;
                int state = intent.getIntExtra(WifiP2pManager.EXTRA_WIFI_STATE, -1);
                String sttStr;
                switch (state) {
                case WifiP2pManager.WIFI_P2P_STATE_ENABLED:
                    mIsWiFiDirectEnabled = true;
                    sttStr = "ENABLED";
                    
                    break;
                case WifiP2pManager.WIFI_P2P_STATE_DISABLED:
                    sttStr = "DISABLED";
                    break;
                default:
                    sttStr = "UNKNOWN";
                    break;
                }
                addLog("state["+sttStr+"]("+state+")");
                

               
                if (mIsWiFiDirectEnabled) {
                    GetSystemService();
                    Initialize();
                }
            } else if (WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION.equals(action)) {
                //addLog("try requestPeers()");
                if (mWifiP2pManager != null) {
                    mWifiP2pManager.requestPeers(mChannel, new WifiP2pManager.PeerListListener() {
                    @Override
                    public void onPeersAvailable(WifiP2pDeviceList peers) {
                  
                        Collection<WifiP2pDevice> p2pdevs = peers.getDeviceList();
                        for(WifiP2pDevice dev : p2pdevs) {
                            if(dev.status == WifiP2pDevice.CONNECTED){
                                mDeviceName = dev.deviceName;
                                Log.d(TAG,"peer device name:" + mDeviceName);
                            }
                        }

                    }
                });
                }
            } else if (WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION.equals(action)) {
                NetworkInfo networkInfo = (NetworkInfo) intent.getParcelableExtra(WifiP2pManager.EXTRA_NETWORK_INFO);
                
                String nlog = networkInfo.toString().replaceAll(",", "　");
                //addLog(nlog);
                
                
                if (networkInfo.isConnected()) {
                    
                    
                    //ADD:20160929
                    if(mConnectState == 0){
                        Log.d(TAG, "AmmboxService:Peer Connected.Ready to connect miracast.");
                        mIsAppBoot = false;
                        mArpRetryCount = 0;
                        invokeSink();
                        mConnectState = 1;
                        
                        //ADD:20160822
                        //mWifiP2pManager.stopPeerDiscovery(mChannel, mActionListenerAdapter);
                        mP2pDiscoverTimer.cancel();
                    }else{
                        Log.d(TAG, "AmmboxService:Peer Connected.Miracast is already connected.");
                    }
                    

                } else if (!mIsAppBoot) {
                    Log.d(TAG, "AmmboxService:Peer Disconnected.");
                    ammboxWfdSinkStop();
                    mIsAppBoot = true;
                    mConnectState = 0;
                    shutdownService();
                    
                }
            } else if (WifiP2pManager.WIFI_P2P_THIS_DEVICE_CHANGED_ACTION.equals(action)) {
                //WifiP2pDevice device = (WifiP2pDevice) intent.getParcelableExtra(WifiP2pManager.EXTRA_WIFI_P2P_DEVICE);
                //mDeviceName = device.deviceName;
                //Log.d(TAG, "mDeviceName 1:"+mDeviceName);               
                //if (mIsWiFiDirectEnabled) {
                //    addLog("Discovering peers 1");
                //    DiscoverPeers();
                //}
            }
        }
    }

    private void shutdownService(){
        Log.d(TAG, "P2P:AmmboxService shutdowned.");
        if(mSurface != null)
            mSurface.release();

        //ADD:20160905
        mCallbacks.kill();
        mP2pDiscoverTimer.cancel();
        System.exit(0); 
    }

    private String getAndroid_ID() {
        return Settings.Secure.getString(getContentResolver(), Settings.Secure.ANDROID_ID);
    }

    
    private String getMACAddress() {
        WifiManager manager = (WifiManager) getSystemService(Context.WIFI_SERVICE);
        WifiInfo wifiInfo = manager.getConnectionInfo();
        String mac = wifiInfo.getMacAddress();
        return mac;
    }
    private boolean hasP2P() {
        return getPackageManager().hasSystemFeature(PackageManager.FEATURE_WIFI_DIRECT);
    }

    private void invokeSink() {
        addLog("invokeSink() call requestGroupInfo()");
        if (isNull(true)) { return; }

        mWifiP2pManager.requestGroupInfo(mChannel, new GroupInfoListener() {
            
            public void onGroupInfoAvailable(WifiP2pGroup group) {
                //addLog("　onGroupInfoAvailable():");
                if (group == null) {
                    addLog("  group is NULL!");
                    return;
                }

                String log = separateCSV(group.toString());

                
                String pass = "　password: ";
                if (group.isGroupOwner()) {
                    pass += group.getPassphrase();
                } else {
                    pass += "Client Couldn't Get Password";
                }
                
                log += pass;
                //addLog(log);

                mP2pControlPort = -1;
                
                Collection<WifiP2pDevice> p2pdevs = group.getClientList();
                
                
                for (WifiP2pDevice dev : p2pdevs) {
                    mDeviceName = dev.deviceName;
                    Log.d(TAG, "mDeviceName 2:"+mDeviceName);
                    boolean b = isWifiDisplaySource(dev);
                    addLog("invokeSink() isWifiDisplaySource("+dev.deviceName+")=["+b+"]");
                    if (!b) {
                        continue;
                        
                    }
                }
                

                if (mP2pControlPort == -1) {
                    
                    mP2pControlPort = 7236;
                    addLog("invokeSink() port=-1?? p2pdevs.size()=["+p2pdevs.size()+"] port assigned=7236");
                }

                
                if (group.isGroupOwner()) { 
                    Log.d(TAG,"AmmboxService:GroupOwner:Local.");
                    mP2pInterfaceName = group.getInterface();
                    
                    mArpTableObservationTimer = new Timer();
                    ArpTableObservationTask task = new ArpTableObservationTask();
                    mArpTableObservationTimer.scheduleAtFixedRate(task, 10, 1*1000);
                    mWifiP2pManager.stopPeerDiscovery(mChannel, mActionListenerAdapter);

                } else { 
                    Log.d(TAG,"AmmboxService:GroupOwner:Remote.");

                    invokeSink2nd();
                    
                    mWifiP2pManager.stopPeerDiscovery(mChannel, mActionListenerAdapter);
                }
            }
        });
    }

    
    private boolean isWifiDisplaySource(WifiP2pDevice dev) {
        if (dev == null || dev.wfdInfo == null) {
            return false;
        }
        WifiP2pWfdInfo wfd = dev.wfdInfo;
        if (!wfd.isWfdEnabled()) {
            return false;
        }

        int type = wfd.getDeviceType();
        mP2pControlPort = wfd.getControlPort();

        boolean source = (type == WifiP2pWfdInfo.WFD_SOURCE) || (type == WifiP2pWfdInfo.SOURCE_OR_PRIMARY_SINK);
        //addLog("isWifiDisplaySource() type["+type+"] is-source["+source+"] port["+mP2pControlPort+"]");
        return source;
    }

    
    private void invokeSink2nd() {
        
        if (isNull(true)) { return; }

        mWifiP2pManager.requestConnectionInfo(mChannel, new ConnectionInfoListener() {
            
            public void onConnectionInfoAvailable(WifiP2pInfo info) {
                
                if (info == null) {
                    addLog("  info is NULL!");
                    return;
                }

                addLog("  groupFormed:" + info.groupFormed);
                addLog("  isGroupOwner:" + info.isGroupOwner);
                addLog("  groupOwnerAddress:" + info.groupOwnerAddress);

                if (!info.groupFormed) {
                    addLog("not yet groupFormed!");
                    return;
                }

                if (info.isGroupOwner) {
                    addLog("Illegal State!!");
                    return;
                } else {
                    String source_ip = info.groupOwnerAddress.getHostAddress();
                    delayedInvokeSink(source_ip, mP2pControlPort, 3);
                }
            }
        });
    }

    
    class P2pDiscoverTask extends TimerTask {
        @Override
        public void run() {
            //addLog("Discovering peers 1");
            if(mConnectState == 0){  //not in connection 
                //addLog("Discovering peers");
                DiscoverPeers();
            }
        }
    }
    

   
    class ArpTableObservationTask extends TimerTask {
        @Override
        public void run() {
            
            RarpImpl rarp = new RarpImpl();
            String source_ip = rarp.execRarp(mP2pInterfaceName);

            
            if (source_ip == null) {
                Log.d(TAG, "retry:" + mArpRetryCount);
                if (++mArpRetryCount > MAX_ARP_RETRY_COUNT) {
                    mArpTableObservationTimer.cancel();
                    return;
                }
                return;
            }

            mArpTableObservationTimer.cancel();
            delayedInvokeSink(source_ip, mP2pControlPort, 3);
        }
    }

    private void delayedInvokeSink(final String ip, final int port, int delaySec) {
        mHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                invokeSink(ip, port);
            }
        }, delaySec*1000);
    }

    public static void invokeSink(String ip, int port) {
        Log.d(TAG, "invokeSink() Source Addr["+ip+":"+port+"]");

        mWfdSinkThread = new AvoidANRThread(ip, port);
        mWfdSinkThread.start();
        
    }

    private static class AvoidANRThread extends Thread {
        
        private final String ip;
        private final int port;

        AvoidANRThread(String _ip, int _port) {
            ip = _ip;
            port = _port;
        }

        public void run() {
                ammboxWfdSink(ip, port, mSurface, mDeviceName);
                
                //Log.d(TAG, "P2P:thread ammboxWfdSink exit.");
            }
    }

    private static native void ammboxWfdSink(String ip, int port, Surface s, String devName);
    private static native void ammboxWfdSinkStop();
    private static native void ammboxWfdSinkSuspend();
    private static native void ammboxWfdSinkSetScale(int type);
    private static native void ammboxWfdSinkCmd(int cmd);
    private static native void ammboxWfdSinkShow(Surface s);
    private static native void ammboxWfdSinkTouchEvent(int type, int x, int y, int w, int h);
    private static native void ammboxWfdSinkSetAudioPath(int type);
    private static native void ammboxWfdSinkSetVolume(int vol);
    private static native void ammboxWfdSinkKeyEvent(int type, int keyCode1, int keyCode2);
    

    static {
        System.loadLibrary("Ammbox");
    }

    
}
