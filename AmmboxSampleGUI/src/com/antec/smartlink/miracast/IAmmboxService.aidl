package com.antec.smartlink.miracast;

import com.antec.smartlink.miracast.IAmmboxServiceCallback;
import android.view.Surface;

interface IAmmboxService {

	/**
	* {@hide}
	*/
    void create();
    /**
	* {@hide}
	*/
    void destroy();
	/**
	* {@hide}
	*/
	void pause();
	/**
	* {@hide}
	*/
    void play();
    /**
	* {@hide}
	*/
    void mute();
    /**
	* {@hide}
	*/
    void unmute();
    /**
	* {@hide}
	*/
	void setSurface(in Surface s);
    /**
	* {@hide}
	*/
    int getState();
    /**
	* {@hide}
	*/
    void suspend();
    /**
	* {@hide}
	*/
    void setScale(int type);
    /**
	* {@hide}
	*/
    void setAudioPath(int path);
    /**
	* {@hide}
	*/
    void setVolume(int vol);
    /**
	* {@hide}
	*/
    void registerCallback(IAmmboxServiceCallback cb);
    /**
	* {@hide}
	*/
    void unregisterCallback(IAmmboxServiceCallback cb);
    /**
	* {@hide}
	*/
    void setTouchEvent(int type,int x, int y, int w, int h);
    /**
	* {@hide}
	*/
    void setInitSpeaker(int type);
    /**
	* {@hide}
	*/
    void sendKeyEvent(int type, int keyCode1, int keyCode2);
    
    
    
}