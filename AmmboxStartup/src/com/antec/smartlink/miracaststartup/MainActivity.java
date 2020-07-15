package com.antec.smartlink.miracaststartup;


import android.util.Log;
import android.os.Bundle;
import android.app.Activity;
import android.content.Intent;
import android.widget.Toast;


public class MainActivity extends Activity {
        
    private final static String TAG = "AmmboxStartup";
    
    @Override 
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.i(TAG,"AmmboxStartup:onCreate");
        setContentView(R.layout.main);
    }

    @Override
    protected void onStart() {
        super.onStart();
        Log.i(TAG,"AmmboxStartup:onStart");
        startService(new Intent("com.antec.smartlink.miracast.IAmmboxService")); 
        Toast.makeText(this,"AmmboxService started",Toast.LENGTH_LONG).show();
        finish();
    }
    
    @Override
    protected void onStop() {
        super.onStop();
        Log.i(TAG,"AmmboxStartup:onStop");
        
    }

    public void onDestroy() {
        super.onDestroy();
        Log.i(TAG,"AmmboxStartup:onDestroy");
        
        
    }  
}
