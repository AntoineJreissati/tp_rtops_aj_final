#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <app_version.h>

// Activer le module de journalisation
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

/* Intervalle pour le clignotement de la LED bleue */
#define BLINK_INTERVAL_MS   500
/* Intervalle pour l'échantillonnage des données du capteur CO₂ */
#define CO2_SAMPLE_INTERVAL_MS 2000

/* Identifiants des noeuds du devicetree pour les LEDs */
#define LED_RED_NODE DT_ALIAS(led0)     // LED rouge
#define LED_GREEN_NODE DT_ALIAS(led1)   // LED verte
#define LED_BLUE_NODE DT_ALIAS(led2)    // LED bleue

#if DT_NODE_HAS_STATUS(LED_RED_NODE, okay)
    #define LED_RED_PIN    DT_GPIO_PIN(LED_RED_NODE, gpios)
    #define LED_RED_FLAGS  DT_GPIO_FLAGS(LED_RED_NODE, gpios)
#else
    #error "Carte non supportée : l'alias led0 (LED rouge) n'est pas défini dans le devicetree"
#endif

#if DT_NODE_HAS_STATUS(LED_GREEN_NODE, okay)
    #define LED_GREEN_PIN    DT_GPIO_PIN(LED_GREEN_NODE, gpios)
    #define LED_GREEN_FLAGS  DT_GPIO_FLAGS(LED_GREEN_NODE, gpios)
#else
    #error "Carte non supportée : l'alias led1 (LED verte) n'est pas défini dans le devicetree"
#endif

#if DT_NODE_HAS_STATUS(LED_BLUE_NODE, okay)
    #define LED_BLUE_PIN    DT_GPIO_PIN(LED_BLUE_NODE, gpios)
    #define LED_BLUE_FLAGS  DT_GPIO_FLAGS(LED_BLUE_NODE, gpios)
#else
    #error "Carte non supportée : l'alias led2 (LED bleue) n'est pas défini dans le devicetree"
#endif

// Section conditionnelle pour LoRaWAN
#ifdef LORAWAN_ENABLED

#include </home/aj/TpRTOS/zephyr/include/zephyr/lorawan/lorawan.h>

/* Fonction d'initialisation de LoRaWAN */
void init_lorawan(void) {
    struct lorawan_join_config join_cfg = {
        .mode = LORAWAN_ACT_OTAA,
        .otaa = {
            .join_eui = CONFIG_LORAWAN_APP_EUI,
            .app_key = CONFIG_LORAWAN_APP_KEY,
        },
        .dev_eui = CONFIG_LORAWAN_DEV_EUI,
    };

    // Démarrage de LoRaWAN
    lorawan_start();
    int ret = lorawan_join(&join_cfg);
    if (ret < 0) {
        printk("Erreur de connexion LoRaWAN : %d\n", ret);
    } else {
        printk("Connexion LoRaWAN réussie\n");
    }
}

#endif  // Fin de LORAWAN_ENABLED

int main(void)
{
    /* Message de démarrage */
    printk("Application de mesure de CO2 d'Antoine %s\n", APP_VERSION_STRING);

    /* Initialisation des LEDs */
    const struct device *const dev_red = DEVICE_DT_GET(DT_GPIO_CTLR(LED_RED_NODE, gpios));
    const struct device *const dev_green = DEVICE_DT_GET(DT_GPIO_CTLR(LED_GREEN_NODE, gpios));
    const struct device *const dev_blue = DEVICE_DT_GET(DT_GPIO_CTLR(LED_BLUE_NODE, gpios));

    if (!device_is_ready(dev_red) || !device_is_ready(dev_green) || !device_is_ready(dev_blue)) {
        printk("Une ou plusieurs LEDs ne sont pas prêtes\n");
        return 0;
    }

    gpio_pin_configure(dev_red, LED_RED_PIN, GPIO_OUTPUT | LED_RED_FLAGS);
    gpio_pin_configure(dev_green, LED_GREEN_PIN, GPIO_OUTPUT | LED_GREEN_FLAGS);
    gpio_pin_configure(dev_blue, LED_BLUE_PIN, GPIO_OUTPUT | LED_BLUE_FLAGS);

#ifdef LORAWAN_ENABLED
    /* Initialisation de LoRaWAN */
    init_lorawan();
#endif

    /* Initialisation du capteur CO₂ SCD4X */
    const struct device *scd4x_dev = DEVICE_DT_GET(DT_NODELABEL(scd4x));
    if (!device_is_ready(scd4x_dev)) {
        printk("Le capteur SCD4X n'est pas prêt\n");
        return 0;
    }

    /* Variables pour le timing */
    int64_t last_sample_time = k_uptime_get();

    while (1) {
        int64_t current_time = k_uptime_get();

        /* Échantillonnage du CO2 toutes les 2 secondes */
        if (current_time - last_sample_time >= CO2_SAMPLE_INTERVAL_MS) {
            last_sample_time = current_time;

            struct sensor_value co2, temperature, humidity;
            int ret = sensor_sample_fetch(scd4x_dev);

            if (ret == 0) {
                ret = sensor_channel_get(scd4x_dev, SENSOR_CHAN_CO2, &co2);
                ret |= sensor_channel_get(scd4x_dev, SENSOR_CHAN_AMBIENT_TEMP, &temperature);
                ret |= sensor_channel_get(scd4x_dev, SENSOR_CHAN_HUMIDITY, &humidity);

                if (ret == 0) {
                    printk("CO2: %d ppm, Temp: %d.%06d C, Hum: %d.%06d %%\n",
                           co2.val1, temperature.val1, temperature.val2,
                           humidity.val1, humidity.val2);

                    /* Logique d'allumage des LEDs selon le taux de CO2 */
                    if (co2.val1 > 1000) {
                        gpio_pin_set(dev_red, LED_RED_PIN, 1);
                        gpio_pin_set(dev_green, LED_GREEN_PIN, 0);
                        gpio_pin_set(dev_blue, LED_BLUE_PIN, 0);
                        printk("CO₂ > 1000 ppm : LED rouge allumée\n");
                    } else if (co2.val1 < 500) {
                        gpio_pin_set(dev_red, LED_RED_PIN, 0);
                        gpio_pin_set(dev_green, LED_GREEN_PIN, 1);
                        gpio_pin_set(dev_blue, LED_BLUE_PIN, 0);
                        printk("CO₂ < 500 ppm : LED verte allumée\n");
                    } else {
                        gpio_pin_set(dev_red, LED_RED_PIN, 0);
                        gpio_pin_set(dev_green, LED_GREEN_PIN, 0);
                        gpio_pin_toggle(dev_blue, LED_BLUE_PIN);
                        printk("500 <= CO₂ <= 1000 ppm : LED bleue clignote\n");
                    }

#ifdef LORAWAN_ENABLED
                    /* Préparation et envoi des données de CO₂ via LoRaWAN */
                    uint8_t co2_data = (uint8_t)co2.val1;
                    ret = lorawan_send(1, &co2_data, sizeof(co2_data), LORAWAN_MSG_UNCONFIRMED);
                    if (ret < 0) {
                        printk("Échec de l'envoi LoRaWAN : %d\n", ret);
                    } else {
                        printk("Données de CO₂ envoyées via LoRaWAN : %d ppm\n", co2.val1);
                    }
#endif
                } else {
                    printk("Erreur lors de la récupération des données du capteur\n");
                }
            } else {
                printk("Erreur lors de l'échantillonnage du capteur\n");
            }
        }

        /* Pause pour limiter la vitesse de boucle en mode clignotement */
        k_msleep(100);
    }

    return 0;
}
