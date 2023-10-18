package com.lucas.tester;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbManager;
import android.util.Log;
import com.hoho.android.usbserial.driver.UsbSerialDriver;
import com.hoho.android.usbserial.driver.UsbSerialPort;
import com.hoho.android.usbserial.driver.UsbSerialProber;
import com.hoho.android.usbserial.util.SerialInputOutputManager;
import java.util.List;
import java.nio.charset.StandardCharsets;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.Reader;
import java.io.StringReader;
import java.util.Scanner;

public final class TesterListener implements SerialInputOutputManager.Listener {
    public static native void dataReceived(byte[] data);

    @Override
    public void onNewData(byte[] data) {
        if (data == null || data.length == 0)
            return;

        dataReceived(data);
    }

    @Override
    public void onRunError(Exception e) {
        Log.d("Lucas", e.getMessage());
    }
}
