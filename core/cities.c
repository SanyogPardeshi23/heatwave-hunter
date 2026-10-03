/* cities.c — label table: city name -> coordinates (nearest grid cell is found at start-up). */
#include "hh.h"

const City CITIES[] = {
    {"Delhi", 28.61f, 77.21f},      {"Mumbai", 19.08f, 72.88f},     {"Kolkata", 22.57f, 88.36f},
    {"Chennai", 13.08f, 80.27f},    {"Bengaluru", 12.97f, 77.59f},  {"Hyderabad", 17.39f, 78.49f},
    {"Ahmedabad", 23.02f, 72.57f},  {"Pune", 18.52f, 73.86f},       {"Jaipur", 26.91f, 75.79f},
    {"Lucknow", 26.85f, 80.95f},    {"Kanpur", 26.45f, 80.33f},     {"Nagpur", 21.15f, 79.09f},
    {"Indore", 22.72f, 75.86f},     {"Bhopal", 23.26f, 77.41f},     {"Patna", 25.59f, 85.14f},
    {"Varanasi", 25.32f, 82.97f},   {"Prayagraj", 25.44f, 81.85f},  {"Agra", 27.18f, 78.01f},
    {"Gwalior", 26.22f, 78.18f},    {"Jhansi", 25.45f, 78.57f},     {"Banda", 25.48f, 80.33f},
    {"Churu", 28.30f, 74.95f},      {"Phalodi", 27.13f, 72.37f},    {"Jodhpur", 26.24f, 73.02f},
    {"Bikaner", 28.02f, 73.31f},    {"Jaisalmer", 26.92f, 70.91f},  {"Sri Ganganagar", 29.92f, 73.88f},
    {"Kota", 25.21f, 75.86f},       {"Udaipur", 24.59f, 73.71f},    {"Chandigarh", 30.73f, 76.78f},
    {"Amritsar", 31.63f, 74.87f},   {"Ludhiana", 30.90f, 75.86f},   {"Dehradun", 30.32f, 78.03f},
    {"Shimla", 31.10f, 77.17f},     {"Srinagar", 34.08f, 74.80f},   {"Leh", 34.15f, 77.58f},
    {"Hisar", 29.15f, 75.72f},      {"Raipur", 21.25f, 81.63f},     {"Bhubaneswar", 20.30f, 85.82f},
    {"Ranchi", 23.34f, 85.31f},     {"Guwahati", 26.14f, 91.74f},   {"Shillong", 25.58f, 91.89f},
    {"Imphal", 24.82f, 93.94f},     {"Visakhapatnam", 17.69f, 83.22f}, {"Vijayawada", 16.51f, 80.65f},
    {"Thiruvananthapuram", 8.52f, 76.94f}, {"Kochi", 9.93f, 76.27f}, {"Madurai", 9.93f, 78.12f},
    {"Coimbatore", 11.02f, 76.96f}, {"Surat", 21.17f, 72.83f},      {"Rajkot", 22.30f, 70.80f},
    {"Bhuj", 23.25f, 69.67f},       {"Panaji", 15.50f, 73.83f},     {"Akola", 20.70f, 77.00f},
    {"Brahmapuri", 20.61f, 79.86f}, {"Chandrapur", 19.96f, 79.30f}, {"Wardha", 20.74f, 78.60f},
    {"Aurangabad", 19.88f, 75.34f}, {"Khajuraho", 24.85f, 79.93f},  {"Ramagundam", 18.76f, 79.48f}
};
const int NCITIES = (int)(sizeof(CITIES) / sizeof(CITIES[0]));
