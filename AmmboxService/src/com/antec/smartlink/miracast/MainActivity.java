package com.antec.smartlink.miracast;

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
import android.app.Activity;
import android.view.View;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.Button;
import android.view.View.OnClickListener;
import android.widget.TextView;
import android.text.method.ScrollingMovementMethod;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.view.ViewStub;


public class MainActivity extends Activity {
        
    private final static String TAG = "AmmboxService";
    @Override 
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_p2p);
    }
   
}
