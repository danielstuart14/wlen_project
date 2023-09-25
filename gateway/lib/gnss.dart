import 'dart:async';

import 'package:location/location.dart';

class Position {
  late double latitude;
  late double longitude;
  late double hdop;

  Position(this.latitude, this.longitude, this.hdop);
}

class GNSS {
  Location location = Location();
  StreamSubscription<LocationData>? positionStream;

  checkServices() async {
    bool serviceEnabled;
    PermissionStatus permissionGranted;
    LocationData locationData;

    serviceEnabled = await location.serviceEnabled();
    if (!serviceEnabled) {
      serviceEnabled = await location.requestService();
      if (!serviceEnabled) {
        return;
      }
    }

    permissionGranted = await location.hasPermission();
    if (permissionGranted == PermissionStatus.denied) {
      permissionGranted = await location.requestPermission();
      if (permissionGranted != PermissionStatus.granted) {
        return;
      }
    }
  }

  Future<bool> startGNSS(Function(Position) callback) async {
    await checkServices();

    if (positionStream != null) {
      return false;
    }

    try {
      positionStream = location.onLocationChanged.listen((LocationData data) {
          if (data.latitude != null && data.longitude != null && data.accuracy != null) {
            callback(Position(data.latitude!, data.longitude!, data.accuracy!));
          }
        });
      location.enableBackgroundMode(enable: true);
    } catch (_) {
      return false;
    }

    return true;
  }

  bool stopGNSS() {
    if (positionStream == null) {
      return false;
    }

    positionStream!.cancel();
    positionStream = null;
    return true;
  }
}