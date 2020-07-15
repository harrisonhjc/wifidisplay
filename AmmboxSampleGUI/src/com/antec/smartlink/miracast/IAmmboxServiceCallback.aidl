package com.antec.smartlink.miracast;

oneway interface IAmmboxServiceCallback {
	
    void handlerStatusEvent(int msgID, String param);
}