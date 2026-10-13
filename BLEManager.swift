import Foundation
import Combine
import CoreBluetooth

/// Connexion BLE avec l'Arduino "Stats R4" et envoi des données
final class BLEManager: NSObject, ObservableObject {

    // UUID identiques à ceux du sketch Arduino
    static let serviceUUID  = CBUUID(string: "7A1E0000-4B5C-4F6D-9E8A-1B2C3D4E5F60")
    static let heureUUID    = CBUUID(string: "7A1E0001-4B5C-4F6D-9E8A-1B2C3D4E5F60")
    static let batterieUUID = CBUUID(string: "7A1E0002-4B5C-4F6D-9E8A-1B2C3D4E5F60")
    static let pasUUID      = CBUUID(string: "7A1E0003-4B5C-4F6D-9E8A-1B2C3D4E5F60")
    static let evtHeureUUID = CBUUID(string: "7A1E0004-4B5C-4F6D-9E8A-1B2C3D4E5F60")
    static let evtTitreUUID = CBUUID(string: "7A1E0005-4B5C-4F6D-9E8A-1B2C3D4E5F60")

    private static let toutesLesCaracs = [heureUUID, batterieUUID, pasUUID, evtHeureUUID, evtTitreUUID]

    /// true quand l'Arduino est connecté et que toutes les caractéristiques sont trouvées
    @Published var pret = false
    @Published var etat = "Démarrage…"

    private var central: CBCentralManager!
    private var peripherique: CBPeripheral?
    private var caracs: [CBUUID: CBCharacteristic] = [:]

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    // MARK: - Envoi

    func envoyer(_ s: Stats) {
        guard pret else { return }

        // Heure locale en secondes (l'Arduino la met directement dans sa RTC)
        let maintenant = Date()
        let heureLocale = Int(maintenant.timeIntervalSince1970) + TimeZone.current.secondsFromGMT(for: maintenant)
        ecrire(Self.uint32(UInt32(heureLocale)), Self.heureUUID)

        // Batterie : [niveau (255 = inconnu), en charge (0/1)]
        ecrire(Data([UInt8(s.batterie ?? 255), s.enCharge ? 1 : 0]), Self.batterieUUID)

        // Pas (0xFFFFFFFF = inconnu)
        ecrire(Self.uint32(s.pas.map { UInt32($0) } ?? .max), Self.pasUUID)

        // Événement (20 octets max par texte)
        ecrire(Self.texte(s.evenementHeure), Self.evtHeureUUID)
        ecrire(Self.texte(s.evenementTitre), Self.evtTitreUUID)
    }

    private func ecrire(_ data: Data, _ uuid: CBUUID) {
        guard let p = peripherique, let c = caracs[uuid] else { return }
        p.writeValue(data, for: c, type: .withResponse)
    }

    /// Entier 32 bits en little-endian, comme l'attend l'Arduino
    private static func uint32(_ valeur: UInt32) -> Data {
        withUnsafeBytes(of: valeur.littleEndian) { Data($0) }
    }

    /// Coupe le texte à 20 octets sans casser les caractères accentués
    private static func texte(_ s: String, max: Int = 20) -> Data {
        var resultat = ""
        for caractere in s {
            let essai = resultat + String(caractere)
            if essai.utf8.count > max { break }
            resultat = essai
        }
        return Data((resultat.isEmpty ? " " : resultat).utf8)
    }

    private func lancerRecherche() {
        guard central.state == .poweredOn else { return }
        etat = "Recherche de l'Arduino…"
        central.scanForPeripherals(withServices: [Self.serviceUUID])
    }
}

// MARK: - CBCentralManagerDelegate

extension BLEManager: CBCentralManagerDelegate {

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:    lancerRecherche()
        case .unauthorized: etat = "Autorise le Bluetooth dans Réglages"
        case .poweredOff:   etat = "Active le Bluetooth"
        default:            etat = "Bluetooth indisponible"
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any],
                        rssi RSSI: NSNumber) {
        peripherique = peripheral
        central.stopScan()
        etat = "Connexion…"
        central.connect(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        etat = "Connecté"
        peripheral.delegate = self
        peripheral.discoverServices([Self.serviceUUID])
    }

    func centralManager(_ central: CBCentralManager,
                        didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        pret = false
        caracs = [:]
        peripherique = nil
        lancerRecherche()
    }

    func centralManager(_ central: CBCentralManager,
                        didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        peripherique = nil
        lancerRecherche()
    }
}

// MARK: - CBPeripheralDelegate

extension BLEManager: CBPeripheralDelegate {

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        for service in peripheral.services ?? [] where service.uuid == Self.serviceUUID {
            peripheral.discoverCharacteristics(Self.toutesLesCaracs, for: service)
        }
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        for c in service.characteristics ?? [] {
            caracs[c.uuid] = c
        }
        pret = Self.toutesLesCaracs.allSatisfy { caracs[$0] != nil }
    }
}
