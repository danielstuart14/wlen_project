use hyper::body::Buf;
use hyper::service::{make_service_fn, service_fn};
use hyper::{header, Body, Error, Request, Response, Server};
use hyper::{Method, StatusCode};

use serde::{Deserialize, Serialize};

use p256::ecdsa::{signature::Verifier, Signature, VerifyingKey};

use mongodb::{bson, bson::doc, options::ClientOptions, Client, Database};

use std::time::Duration;

use chrono::{DateTime, NaiveDateTime, TimeZone, Utc};

use std::convert::Infallible;
use std::env;

#[derive(Serialize, Deserialize, Debug)]
struct Message {
    timestamp: u64,
    rssi: i8,
    advertising: String,
    latitude: f64,
    longitude: f64,
    hdop: f64,
}

struct Advertising {
    identifier: String,
    payload: Vec<u8>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
struct Position {
    identifier: String,
    #[serde(with = "bson::serde_helpers::chrono_datetime_as_bson_datetime")]
    scan_date: DateTime<Utc>,
    #[serde(with = "bson::serde_helpers::chrono_datetime_as_bson_datetime")]
    received_date: DateTime<Utc>,
    rssi: i8,
    payload: Vec<u8>,
    latitude: f64,
    longitude: f64,
    hdop: f64,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
struct Beacon {
    identifier: String,
    public_key: String,
}

fn verify_signature(advertising: &String, key: &String) -> bool {
    let adv_bytes = match hex::decode(advertising) {
        Ok(bytes) => bytes,
        Err(_) => return false,
    };

    let key_bytes = match hex::decode(key) {
        Ok(bytes) => bytes,
        Err(_) => return false,
    };

    if adv_bytes.len() <= 64 {
        return false;
    }

    let data_bytes = &adv_bytes[0..(adv_bytes.len() - 64)];
    let sig_bytes = &adv_bytes[(adv_bytes.len() - 64)..];

    let verifying_key = match VerifyingKey::from_sec1_bytes(&key_bytes) {
        Ok(key) => key,
        Err(_) => return false,
    };
    let signature = match Signature::from_bytes(sig_bytes.into()) {
        Ok(sig) => sig,
        Err(_) => return false,
    };
    let status = verifying_key.verify(data_bytes, &signature);

    status.is_ok()
}

fn parse_advertising(message: &Message) -> Option<Advertising> {
    match hex::decode(message.advertising.clone()) {
        Ok(data) => {
            if data.len() < 9 {
                return None;
            }

            let mut id_bytes = [0u8; 6];
            id_bytes.copy_from_slice(&data[0..6]);

            let identifier = hex::encode(id_bytes);
            let length = data[6];

            if data.len() != (6 + 1 + length as usize + 64) {
                return None;
            }

            let mut payload = Vec::new();
            payload.extend(&data[7..7 + length as usize]);

            Some(Advertising {
                identifier,
                payload,
            })
        }
        Err(_) => None,
    }
}

async fn post_handler(req: Request<Body>, db: Database) -> Result<Response<Body>, Infallible> {
    println!("Received!");
    let datetime = Utc::now();
    let mut builder = Response::builder();

    let content = req.headers().get(header::CONTENT_TYPE);

    match content {
        Some(content) => {
            if content.to_str().unwrap().contains("application/json") {
                let req_body = hyper::body::aggregate(req).await.unwrap();
                let message: Result<Message, serde_json::Error> =
                    serde_json::from_reader(req_body.reader());
                match message {
                    Ok(message) => match parse_advertising(&message) {
                        Some(advertising) => {
                            let beacons = db.collection::<Beacon>("beacons");
                            let beacon = beacons
                                .find_one(Some(doc! {"identifier": &advertising.identifier}), None)
                                .await
                                .unwrap();
                            match beacon {
                                Some(beacon) => {
                                    if verify_signature(&message.advertising, &(beacon.public_key))
                                    {
                                        let d = Duration::from_millis(message.timestamp);
                                        match NaiveDateTime::from_timestamp_opt(
                                            d.as_secs().try_into().unwrap(),
                                            d.subsec_nanos() as u32,
                                        ) {
                                            Some(naivedt) => {
                                                let positions =
                                                    db.collection::<Position>("positions");
                                                let position = Position {
                                                    identifier: advertising.identifier,
                                                    scan_date: Utc.from_utc_datetime(&naivedt),
                                                    received_date: datetime,
                                                    rssi: message.rssi,
                                                    payload: advertising.payload,
                                                    latitude: message.latitude,
                                                    longitude: message.longitude,
                                                    hdop: message.hdop,
                                                };

                                                positions.insert_one(position, None).await.unwrap();
                                                println!("OK!");
                                            }
                                            None => {
                                                println!("Invalid timestamp!");
                                                builder = builder.status(StatusCode::BAD_REQUEST);
                                            }
                                        }
                                    } else {
                                        builder = builder.status(StatusCode::UNAUTHORIZED);
                                    }
                                }
                                None => {
                                    println!("Not found!");
                                    builder = builder.status(StatusCode::NOT_FOUND);
                                }
                            }
                        }
                        None => {
                            builder = builder.status(StatusCode::BAD_REQUEST);
                        }
                    },
                    Err(e) => {
                        println!("Error parsing JSON: {}", e);
                        builder = builder.status(StatusCode::BAD_REQUEST);
                    }
                }
            } else {
                builder = builder.status(StatusCode::UNSUPPORTED_MEDIA_TYPE);
            }
        }
        None => {
            builder = builder.status(StatusCode::BAD_REQUEST);
        }
    }

    let response = builder.body(Body::from("".to_string())).unwrap();
    Ok(response)
}

async fn handler(req: Request<Body>, db: Database) -> Result<Response<Body>, Infallible> {
    let response = match (req.method(), req.uri().path()) {
        (&Method::POST, "/api/wlen") => post_handler(req, db).await.unwrap(),

        _ => Response::builder()
            .status(StatusCode::NOT_FOUND)
            .body(Body::from(""))
            .unwrap(),
    };

    Ok(response)
}

#[tokio::main]
async fn main() -> Result<(), Error> {
    let port_key = "FUNCTIONS_CUSTOMHANDLER_PORT";
    let port: u16 = match env::var(port_key) {
        Ok(val) => val.parse().expect("Custom Handler port is not a number!"),
        Err(_) => 3000,
    };
    let addr = ([127, 0, 0, 1], port).into();

    let client_options = ClientOptions::parse(std::env::var("DB_URI").unwrap())
        .await
        .unwrap();
    let client = Client::with_options(client_options).unwrap();
    let db = client.database("wlen");

    let server = Server::bind(&addr).serve(make_service_fn(move |_conn| {
        let db = db.clone();
        async move { Ok::<_, Infallible>(service_fn(move |req| handler(req, db.clone()))) }
    }));

    println!("Listening on http://{}", addr);

    server.await
}
