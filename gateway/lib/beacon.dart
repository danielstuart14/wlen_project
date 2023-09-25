import 'dart:typed_data';

import 'package:convert/convert.dart';

class Beacon {
  String identifier;
  int rssi;
  DateTime datetime;
  Uint8List data;

  Beacon(this.identifier, this.rssi, this.datetime, this.data);

  Map<String, String> parseData() {
    Map<String, String> map = {};
    map["identifier"] = hex.encode(data.sublist(0, 6));

    var length = data[6];
    map["payload"] = hex.encode(data.sublist(7, 7 + length));
    map["signature"] = hex.encode(data.sublist(7 + length));
    map["elapsed"] = DateTime.now().difference(datetime).inSeconds.toString();

    return map;
  }

  String hexData() {
    return hex.encode(data);
  }
}