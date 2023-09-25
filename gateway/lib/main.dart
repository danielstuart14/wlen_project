import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter/material.dart';
import 'request.dart';
import 'beacon.dart';
import 'bluetooth.dart';
import 'gnss.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({Key? key}) : super(key: key);

  // This widget is the root of your application.
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Flutter Demo',
      theme: ThemeData.dark(),
      home: const MyHomePage(title: 'Gateway'),
    );
  }
}

class MyHomePage extends StatefulWidget {
  const MyHomePage({Key? key, required this.title}) : super(key: key);

  // This widget is the home page of your application. It is stateful, meaning
  // that it has a State object (defined below) that contains fields that affect
  // how it looks.

  // This class is the configuration for the state. It holds the values (in this
  // case the title) provided by the parent (in this case the App widget) and
  // used by the build method of the State. Fields in a Widget subclass are
  // always marked "final".

  final String title;

  @override
  State<MyHomePage> createState() => _MyHomePageState();
}

class _MyHomePageState extends State<MyHomePage> {
  final bluetooth = Bluetooth();
  List<Beacon> beacons = [];
  GNSS gnss = GNSS();
  Position position = Position(0.0, 0.0, 999.9);
  Request request = Request();

  void beaconFound(Beacon beacon) {
    var index = beacons.indexWhere((element) => element.identifier == beacon.identifier);
    if (index >= 0) {
      beacons[index] = beacon;
    } else {
      beacons.add(beacon);
    }

    var reqData = RequestData(beacon, position);
    request.send(reqData);

    setState(() {});
  }

  void newPosition(Position position) {
    // if (kDebugMode) {
    //   print("New position!");
    //   print("    Latitude: ${position.latitude}");
    //   print("    Longitude: ${position.longitude}");
    //   print("    HDOP: ${position.hdop}");
    // }
    this.position = position;

    setState(() {});
  }

  @override
  void initState() {
    bluetooth.startScan(beaconFound);
    gnss.startGNSS(newPosition);

    super.initState();
  }

  Future<void> showInfo(Beacon beacon) async {
    var data = beacon.parseData();

    return showDialog<void>(
      context: context,
      barrierDismissible: false, // user must tap button!
      builder: (BuildContext context) {
        return AlertDialog(
          title: Text(beacon.identifier),
          content: SingleChildScrollView(
            child: Column(
              children: [
                ListTile(
                  leading: const Icon(Icons.perm_identity),
                  title: const Text("Identifier"),
                  subtitle: Text(data["identifier"]!),
                ),
                ListTile(
                  leading: const Icon(Icons.file_copy),
                  title: const Text("Payload"),
                  subtitle: Text(data["payload"]!),
                ),
                ListTile(
                  leading: const Icon(Icons.timelapse),
                  title: const Text("Time elapsed"),
                  subtitle: Text(data["elapsed"]!),
                ),
                ListTile(
                  leading: const Icon(Icons.signal_cellular_alt),
                  title: const Text("Signal"),
                  subtitle: Text(beacon.rssi.toString()),
                ),
              ],
            )
          ),
          actions: <Widget>[
            TextButton(
              child: const Text('Copiar'),
              onPressed: () {
                Clipboard.setData(ClipboardData(text: beacon.hexData()));
                Navigator.of(context).pop();
              }
            ),
            TextButton(
              child: const Text('Fechar'),
              onPressed: () {
                Navigator.of(context).pop();
              },
            ),
          ],
        );
      },
    );
  }

  List<Widget> getBeacons() {
    List <Widget> widgets = [];

    var now = DateTime.now();
    for (Beacon beacon in beacons) {
      var isOld = now.difference(beacon.datetime).inSeconds >= 15;
      Widget widget = Opacity(
        opacity: (isOld) ? 0.4 : 1.0,
        child: Card(
          child: Padding(
            padding: const EdgeInsets.fromLTRB(20, 10, 20, 10),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Text(beacon.identifier),
                ElevatedButton(
                  onPressed: (isOld) ? null : () {showInfo(beacon);},
                  child: const Text("Informações"),
                )
              ]
            )
          )
        )
      );
      widgets.add(widget);
    }

    return widgets;
  }

  @override
  Widget build(BuildContext context) {

    return Scaffold(
      appBar: AppBar(
        title: Text(widget.title),
      ),
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.start,
          children: getBeacons()
        ),
      ),
      bottomNavigationBar: BottomAppBar(
        child: Text("HDOP: ${position.hdop.toStringAsFixed(2)}", textAlign: TextAlign.center)
      ),
      floatingActionButton: FloatingActionButton(
        onPressed: () {
          beacons = [];
          setState((){});
        },
        child: const Icon(Icons.restart_alt),
      ),
    );
  }
}
