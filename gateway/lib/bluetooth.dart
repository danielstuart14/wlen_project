import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';
import 'beacon.dart';

class Bluetooth {
  final _ble = FlutterReactiveBle();
  StreamSubscription<DiscoveredDevice>? scanStream;

  bool startScan(Function(Beacon) callback) {
    if (scanStream != null) {
      return false;
    }

    try {
      scanStream = _ble.scanForDevices( withServices: [Uuid.parse("1408")], scanMode: ScanMode.lowLatency).listen((device) {
        if (device.manufacturerData.length < 2) {
          return;
        }

        var data = device.manufacturerData.sublist(2);

        // if (kDebugMode) {
        //   print("New advertising received:");
        //   print("    ID: ${device.id}");
        //   print("    RSSI: ${device.rssi}");
        //   print("    DATA: $data");
        // }

        callback(Beacon(device.id, device.rssi, DateTime.now(), data));
      });
    } catch (_) {
      return false;
    }

    return true;
  }

  bool stopScan() {
    if (scanStream == null) {
      return false;
    }

    scanStream?.cancel();
    scanStream = null;
    return true;
  }
}