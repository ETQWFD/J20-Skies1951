package com.changkong1951.skies;
public class NativeLoader extends android.app.NativeActivity {
    static { System.loadLibrary("main"); }
}
