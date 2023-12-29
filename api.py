from fastapi import FastAPI, HTTPException, Form, Depends, Query, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import HTMLResponse
from pydantic import BaseModel


from pymongo.mongo_client import MongoClient
from pymongo.server_api import ServerApi
import matplotlib.pyplot as plt
import numpy as np
import io
import base64
from fastapi.templating import Jinja2Templates
from datetime import datetime  # Import the datetime module

app = FastAPI()

# Enable CORS
origins = ["*"]
app.add_middleware(
    CORSMiddleware,
    allow_origins=origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Replace the following with your MongoDB connection details
uri = "mongodb+srv://baris:baris@iot.mpbeoff.mongodb.net/?retryWrites=true&w=majority"
client = MongoClient(uri, server_api=ServerApi("1"))
db = client[
    "your_database_name"
]  # Replace 'your_database_name' with your actual database name
collection = db["sensor_data"]
templates = Jinja2Templates(directory="templates")


class SensorData(BaseModel):
    weight: float
    temperature: float


@app.post("/api/sensor")
async def receive_sensor_data(
    temperature=Query(0, description="Skip items"),
    weight=Query(0, description="Limit items"),
):
    print(temperature, weight)
    try:
        # Insert sensor data into the MongoDB collection
        current_time = datetime.now()
        result = collection.insert_one(
            {
                "temperature": temperature,
                "weight": weight,
                "timestamp": current_time,  # Add current time to the data
            }
        )
        print(f"Data saved to MongoDB with ID: {result.inserted_id}")
    except Exception as e:
        print(f"Error saving data to MongoDB: {e}")
        raise HTTPException(status_code=500, detail="Internal Server Error")

    return {"message": "Data received and saved successfully"}


@app.get("/dashboard")
async def dashboard(request: Request):
    try:
        # Fetch all sensor data from MongoDB
        cursor = collection.find(
            {}, {"_id": 0, "temperature": 1, "weight": 1, "timestamp": 1}
        )
        sensor_data = list(cursor)

        # Extract data for plots
        timestamps = [data["timestamp"] for data in sensor_data]
        temperatures = [data["temperature"] for data in sensor_data]
        weights = [data["weight"] for data in sensor_data]

        # Create a plot for time vs weight
        plt.figure(figsize=(8, 4))
        plt.plot(timestamps, weights, marker="o", linestyle="-", color="blue")
        plt.xlabel("Time")
        plt.ylabel("Weight")
        plt.title("Time vs Weight")

        # Save the weight plot to a BytesIO object
        weight_stream = io.BytesIO()
        plt.savefig(weight_stream, format="png")
        weight_stream.seek(0)
        weight_base64 = base64.b64encode(weight_stream.read()).decode("utf-8")

        # Create a plot for time vs temperature
        plt.figure(figsize=(8, 4))
        plt.plot(timestamps, temperatures, marker="o", linestyle="-", color="red")
        plt.xlabel("Time")
        plt.ylabel("Temperature")
        plt.title("Time vs Temperature")

        # Save the temperature plot to a BytesIO object
        temperature_stream = io.BytesIO()
        plt.savefig(temperature_stream, format="png")
        temperature_stream.seek(0)
        temperature_base64 = base64.b64encode(temperature_stream.read()).decode("utf-8")

        plt.figure(figsize=(8, 4))
        plt.plot(temperatures, weights, marker="o", linestyle="-", color="blue")
        plt.xlabel("Temperatures")
        plt.ylabel("Weight")
        plt.title("Temperatures vs Weight")

        # Save the weight plot to a BytesIO object
        temp_weight_stream = io.BytesIO()
        plt.savefig(temp_weight_stream, format="png")
        temp_weight_stream.seek(0)
        temp_weight_base64 = base64.b64encode(temp_weight_stream.read()).decode("utf-8")

        # Render the HTML template with dynamic content
        return templates.TemplateResponse(
            "dashboard.html",
            {
                "request": request,
                "weight_base64": weight_base64,
                "temperature_base64": temperature_base64,
                "temp_weight_base64": temp_weight_base64,
                "sensor_data": sensor_data,
            },
        )
    except Exception as e:
        print(f"Error fetching data from MongoDB: {e}")
        raise HTTPException(status_code=500, detail="Internal Server Error")


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="172.16.55.35", port=8000)
