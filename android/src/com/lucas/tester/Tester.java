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
import java.nio.charset.StandardCharsets;
import java.io.IOException;
import java.util.List;

public final class Tester {
    public static UsbSerialDriver mDriver;
    private static UsbDeviceConnection mConnection;
    public static UsbSerialPort mSerialPort;
    public static SerialInputOutputManager mIoManager;
    public static BroadcastReceiver mConnectBroadcastReceiver;
    public static BroadcastReceiver mDisconnectBroadcastReceiver;
    public static TesterListener mListener;
    public static Context mContext;
    private static final String INTENT_ACTION_GRANT_USB = "com.lucas.tester.GRANT_USB";
    private static final String INTENT_ACTION_DISCONNECT = "com.lucas.tester.Disconnect";

    public static void create(Context context, boolean granted) {
        mContext = context;
        mConnectBroadcastReceiver = new BroadcastReceiver() {
            public void onReceive(Context context, Intent intent) {
                if (INTENT_ACTION_GRANT_USB.equals(intent.getAction())) {
                    boolean granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false);
                    connect(granted);
                }

            }
        };
        connect(false);
    }

    public static void send(byte[] data) {
        if (mSerialPort == null)
            return;

        try {
            mSerialPort.write(data, 0);
        } catch (IOException e) {
            Log.e("Lucas", "Falha ao enviar comando");
        }
    }

    public static void connect(boolean granted) {
        UsbManager manager = (UsbManager) mContext.getSystemService(Context.USB_SERVICE);
        Log.d("Lucas", "LISTA: \n" + manager.getDeviceList());
        List<UsbSerialDriver> drivers = UsbSerialProber.getDefaultProber().findAllDrivers(manager);
        Log.d("Lucas", "DRIVERS: \n" + drivers);
        UsbSerialDriver foundDriver = null;
        for (UsbSerialDriver driver: drivers) {
            if (driver == null)
                continue;

            UsbDevice device = driver.getDevice();
            if (device == null)
                continue;

            if (device.getVendorId() == 1155) {
                foundDriver = driver;
                break;
            }
        }

        if (foundDriver == null) {
            Log.e("Lucas", "Falha ao achar um driver legal");
            return;
        }

        mDriver = foundDriver;
        mConnection = manager.openDevice(mDriver.getDevice());
        if (mConnection == null && !granted && !manager.hasPermission(mDriver.getDevice())) {
            PendingIntent intent = PendingIntent.getBroadcast(mContext, 0, new Intent(INTENT_ACTION_GRANT_USB), PendingIntent.FLAG_MUTABLE);
            manager.requestPermission(mDriver.getDevice(), intent);
        }

        if (mConnection == null) {
            if (!manager.hasPermission(mDriver.getDevice())) {
                Log.e("Lucas", "Erro ao abrir - permissão");
            } else {
                Log.e("Lucas", "Erro ao abrir");
            }
            return;
        }

        mSerialPort = foundDriver.getPorts().get(0);

        try {
            mSerialPort.open(mConnection);
            mSerialPort.setParameters(115200, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE);
            mSerialPort.setDTR(true);
            mSerialPort.setRTS(true);
            mListener = new TesterListener();
            mIoManager = new SerialInputOutputManager(mSerialPort, mListener);
            mIoManager.start();
        } catch (Exception e) {
            Log.e("Lucas", "Exception ao conectar " + e.getMessage());
        }
    }
}
