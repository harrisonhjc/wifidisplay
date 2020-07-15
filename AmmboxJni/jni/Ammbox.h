#ifndef AMMBOX_H_
#define AMMBOX_H_


namespace android {


struct Ammbox : public Thread {

    void start();
    void stop();
    virtual ~Ammbox();
    virtual bool threadLoop();

private:
    bool stopAll;    
};

}  // namespace android

#endif  // AMMBOX_H_