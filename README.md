# WLEN (Wireless Low-Energy Network) project
Capstone project for my computer engineering bachelor.
The project consists of a system capable of opportunistically reading sensor values from BLE tags (beacons) by a smartphone (gateway) and returning it to a backend with geolocation data associated.

![Topology](/assets/topology.png)

It uses ECDSA for authentication of the BLE tag by the server, taking advantage of the extended advertising size available on BLE 5.0+.

![BLE Protocol](/assets/protocol.png)

The BLE tag (beacon) is a Holyiot 21014-V1.0, based in the nRF52810 chipset. The software was developed using Zephyr.

![BLE Tag](/assets/bletag.png)

The application running in the smartphone (gateway) was made using Flutter and has the following class diagram:

![Class diagram](/assets/classes.png)

The backend was developed with Rust using Azure's cloud services. The HTTP parser runs inside a Azure Function and all the data is saved in a Azure Cosmos DB.

![Backend](/assets/backend.png)

The smartphone app sends a HTTP post to the server with the following data:
![Api](/assets/api.png)

The database has two collections, one for the beacon registratiob and the other for the positions received.
![Database](/assets/database.png)

Here is an example of a plot of the data received from two moving beacons and 4 fixed gateways:
![Plot](/assets/plot.png)
