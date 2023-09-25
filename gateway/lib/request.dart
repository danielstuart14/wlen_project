import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;

import 'package:convert/convert.dart';


import 'beacon.dart';
import 'gnss.dart';

const String BACKEND_URL = "https://wlen.azurewebsites.net/api/wlen";

class RequestData {
  Beacon beacon;
  Position position;

  RequestData(this.beacon, this.position);
}

class Request {
  final _queue = Queue<RequestData>();
  Timer? _timer;

  Request() {
    if (_timer != null && _timer!.isActive) {
      _timer!.cancel();
    }
    _isConsuming = false;
  }

  bool _isConsuming = false;
  void _consume() async {
    if (_isConsuming) {
      return;
    }
    _isConsuming = true;

    try {
      if (_timer != null && _timer!.isActive) {
        _timer!.cancel();
      }

      if (kDebugMode) {
        print("Sending buffer to server:");
      }

      while (_queue.isNotEmpty) {
        RequestData data = _queue.first;
        var body = jsonEncode({
          "timestamp": data.beacon.datetime.millisecondsSinceEpoch,
          "rssi": data.beacon.rssi,
          "advertising": hex.encode(data.beacon.data),
          "latitude": data.position.latitude,
          "longitude": data.position.longitude,
          "hdop": data.position.hdop
        });

        var url = Uri.parse(BACKEND_URL);
        var headers =  <String, String>{'Content-Type': 'application/json'};
        try {
          final response = await http.post(url, body: body, headers: headers).timeout(const Duration(seconds: 5));
          if (kDebugMode) {
            print("    Request respose: ${response.statusCode}");
          }
        } catch (e) {
          if (kDebugMode) {
            print("    Error while sending data: $e");
          }
          break;
        }
        if (kDebugMode) {
          print("Successfully sent data!");
        }
        _queue.removeFirst();
      }

      if (_queue.isNotEmpty) {
        _timer = Timer.periodic(
            const Duration(seconds: 5), (Timer t) => {_consume()});
      }
    } finally {
      _isConsuming = false;
    }
  }

  void send(RequestData data) {
    if (kDebugMode) {
      print("Adding data to be sent!");
    }
    _queue.add(data);
    if (_timer == null || !_timer!.isActive) {
      _consume();
    }
  }
}